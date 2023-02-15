#include "driver.hh"

#include <spdlog/spdlog.h>

#include <chrono>

using namespace std::chrono_literals;

namespace driver {

auto DevConfig::Format() const -> std::string {
  return fmt::format("trtype:{} adrfam:{} traddr:{} trsvcid:{} subnqn:{}",
                     (is_rdma ? "rdma" : "pcie"), (is_ipv4 ? "ipv4" : "ipv6"),
                     addr, svc_id,
                     (subnqn.empty() ? SPDK_NVMF_DISCOVERY_NQN : subnqn));
}

Dev::Dev(boost::asio::io_context &ctx)
    : trid_(new spdk_nvme_transport_id), coro_ctx(ctx) {}

Dev::~Dev() { delete trid_; }

auto Dev::PollAdminQueue() -> boost::asio::awaitable<void> {
  SPDLOG_INFO("start polling admin queue");

  int rc = 0;
  auto reason = SPDK_NVME_QPAIR_FAILURE_NONE;
  boost::asio::high_resolution_timer interval_timer(coro_ctx, 10us);
  while (admin_poller_running_.load(std::memory_order_relaxed)) {
    rc = spdk_nvme_ctrlr_process_admin_completions(ctrlr_);
    if (rc < 0) {
      SPDLOG_ERROR("get error when polling admin queue, rc: {}", rc);
    } else {
      reason = spdk_nvme_ctrlr_get_admin_qp_failure_reason(ctrlr_);
      if (reason != SPDK_NVME_QPAIR_FAILURE_NONE) {
        SPDLOG_ERROR("get failure when polling admin queue, reason: {}",
                     int(reason));
      }
    }
    co_await interval_timer.async_wait(boost::asio::use_awaitable);
  }
}

auto Dev::PollIoQueue() -> boost::asio::awaitable<void> {
  SPDLOG_INFO("start polling io queue");

  int rc = 0;
  auto reason = SPDK_NVME_QPAIR_FAILURE_NONE;
  boost::asio::high_resolution_timer interval_timer(coro_ctx, 1ns);
  while (io_poller_running_.load(std::memory_order_relaxed)) {
    rc = spdk_nvme_qpair_process_completions(qpair_, max_depth_);
    if (rc < 0) {
      reason = spdk_nvme_qpair_get_failure_reason(qpair_);
      if (reason != SPDK_NVME_QPAIR_FAILURE_NONE) {
        SPDLOG_ERROR("get failure when polling qpair, reason: {}, rc: {}",
                     int(reason), rc);
      }
    }
    co_await interval_timer.async_wait(boost::asio::use_awaitable);
  }
  co_return;
}

auto Dev::Attach(const DevConfig &config) -> Status {
  //* parse config
  auto formatted_config = config.Format();
  if (auto rc = spdk_nvme_transport_id_parse(trid_, formatted_config.c_str());
      rc != 0) {
    SPDLOG_ERROR("fail to parse nvme transport id, rc: {}", rc);
    return Status::InvalidArgs();
  }
  SPDLOG_TRACE("formatted config: {}", formatted_config);

  //* probe device
  auto probe_cb = [](void *, const spdk_nvme_transport_id *trid,
                     spdk_nvme_ctrlr_opts *) -> bool {
    SPDLOG_INFO("attaching to {}:{}", std::string_view(trid->traddr),
                std::string_view(trid->trsvcid));
    return true;
  };
  auto attach_cb = [](void *ctx, const spdk_nvme_transport_id *trid,
                      spdk_nvme_ctrlr *ctrlr,
                      const spdk_nvme_ctrlr_opts *) -> void {
    ((Dev *)ctx)->ctrlr_ = ctrlr;
    SPDLOG_INFO("attached to {}:{}", std::string_view(trid->traddr),
                std::string_view(trid->trsvcid));
    auto ctrlr_data = spdk_nvme_ctrlr_get_data(ctrlr);
    SPDLOG_INFO("SN: {:20}",
                std::string_view(
                    (const char *)ctrlr_data->sn,
                    (const char *)(ctrlr_data->sn + SPDK_NVME_CTRLR_SN_LEN)));
    SPDLOG_INFO("MN: {:40}",
                std::string_view(
                    (const char *)ctrlr_data->mn,
                    (const char *)(ctrlr_data->mn + SPDK_NVME_CTRLR_MN_LEN)));
    SPDLOG_INFO("FR: {:8}",
                std::string_view(
                    (const char *)ctrlr_data->fr,
                    (const char *)(ctrlr_data->fr + SPDK_NVME_CTRLR_FR_LEN)));
    SPDLOG_INFO("subnqn: {:8}",
                std::string_view((const char *)ctrlr_data->subnqn,
                                 (const char *)(ctrlr_data->subnqn +
                                                SPDK_NVME_NQN_FIELD_SIZE)));
  };
  if (auto rc = spdk_nvme_probe(trid_, this, probe_cb, attach_cb, nullptr);
      rc != 0) {
    SPDLOG_ERROR("fail to probe nvme device, rc: {}", rc);
    return Status::DeviceFailure();
  }

  //* get namespace
  for (auto ns_id = spdk_nvme_ctrlr_get_first_active_ns(ctrlr_); ns_id != 0;
       ns_id = spdk_nvme_ctrlr_get_next_active_ns(ctrlr_, ns_id)) {
    if (ns_id != config.ns_id) continue;
    ns_ = spdk_nvme_ctrlr_get_ns(ctrlr_, ns_id);
    if (ns_ == nullptr) continue;
    n_sector_ = spdk_nvme_ns_get_num_sectors(ns_);
    sector_size_ = spdk_nvme_ns_get_sector_size(ns_);
    max_xfer_size_ = spdk_nvme_ns_get_max_io_xfer_size(ns_);
    break;  // only use one namespace
  }
  if (ns_ == nullptr) {
    SPDLOG_ERROR("fail to find namespace{} in {}, detach", config.ns_id,
                 std::string_view(trid_->traddr));
    spdk_nvme_detach(ctrlr_);
    return Status::DeviceFailure();
  }
  SPDLOG_INFO("got namespace {}, size: {} GiB", config.ns_id,
              n_sector_ * sector_size_ / 1000 / 1000 / 1000);

  //* allocate qpair
  spdk_nvme_io_qpair_opts opts{};
  spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr_, &opts, sizeof(opts));
  opts.delay_cmd_submit = true;  // batch submit
  max_depth_ = opts.io_queue_requests - 1;
  qpair_ = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr_, &opts, sizeof(opts));
  if (qpair_ == nullptr) {
    SPDLOG_ERROR("fail to allocate qpair in {}, detach",
                 std::string_view(trid_->traddr));
    spdk_nvme_detach(ctrlr_);
    return Status::DeviceFailure();
  }
  SPDLOG_TRACE("created a qpair");

  //* admin poller
  admin_poller_running_ = true;
  boost::asio::co_spawn(coro_ctx, PollAdminQueue(), boost::asio::detached);

  //* io poller
  io_poller_running_ = true;
  boost::asio::co_spawn(coro_ctx, PollIoQueue(), boost::asio::detached);

  SPDLOG_INFO("succeeded to initialize the driver");
  return Status::OK();
}

auto Dev::Detach() -> Status {
  spdk_nvme_ctrlr_disconnect_io_qpair(qpair_);
  spdk_nvme_ctrlr_free_io_qpair(qpair_);
  spdk_nvme_detach(ctrlr_);
  return Status::OK();
}

auto Dev::IO::Issue(spdk_nvme_cmd_cb cb, void *cb_arg) -> void {
  // submit
  int rc = 0;
  switch (type) {
    case IO::Type::Read: {
      rc = spdk_nvme_ns_cmd_read(dev->ns_, dev->qpair_, payload, lba, lba_count,
                                 cb, cb_arg, io_flags);
      break;
    }
    case IO::Type::Write: {
      rc = spdk_nvme_ns_cmd_write(dev->ns_, dev->qpair_, payload, lba,
                                  lba_count, cb, cb_arg, io_flags);
      break;
    }
  }

  // may be wrong
  if (rc < 0) {
    if (rc == -EINVAL) {
      status = Status::InvalidArgs();
    } else if (rc == -ENOMEM) {
      status = Status::IoQueueFull();
    } else if (rc == -ENXIO) {
      status = Status::DeviceFailure();
    }
  }
}

auto Dev::Read(void *buf, uint64_t start_lba, uint32_t n_lba, uint32_t flags)
    -> boost::asio::awaitable<Status> {
  IO io(IO::Type::Read, this, start_lba, n_lba, buf, flags);
  co_await AsyncSpdkNvmeCmd(&io, boost::asio::use_awaitable);
  co_return io.GetStatus();
}

auto Dev::Write(void *buf, uint64_t start_lba, uint32_t n_lba, uint32_t flags)
    -> boost::asio::awaitable<Status> {
  IO io(IO::Type::Write, this, start_lba, n_lba, buf, flags);
  co_await AsyncSpdkNvmeCmd(&io, boost::asio::use_awaitable);
  co_return io.GetStatus();
}

}  // namespace driver