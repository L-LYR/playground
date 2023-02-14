#include "driver.hh"

#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/compose.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>

using namespace std::chrono_literals;

namespace driver {

auto DevConfig::Format() const -> std::string {
  return fmt::format("trtype:{} adrfam:{} traddr:{} trsvcid:{} subnqn:{}",
                     (is_rdma ? "rdma" : "pcie"), (is_ipv4 ? "ipv4" : "ipv6"),
                     addr, svc_id,
                     (subnqn.empty() ? SPDK_NVMF_DISCOVERY_NQN : subnqn));
}

Dev::Dev() : coro_ctx(1), strand_(asio::make_strand(coro_ctx.get_executor())) {}

Dev::~Dev() {}

auto Dev::PollAdminQueue() -> asio::awaitable<void> {
  int rc = 0;
  auto reason = SPDK_NVME_QPAIR_FAILURE_NONE;
  asio::steady_timer interval_timer(coro_ctx, 10us);
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
    co_await interval_timer.async_wait(asio::use_awaitable);
  }
  co_return;
}

auto Dev::PollIoQueue() -> asio::awaitable<void> {
  int rc = 0;
  auto reason = SPDK_NVME_QPAIR_FAILURE_NONE;
  while (io_poller_running_.load(std::memory_order_relaxed)) {
    rc = spdk_nvme_qpair_process_completions(qpair_, max_depth_);
    if (rc < 0) {
      reason = spdk_nvme_qpair_get_failure_reason(qpair_);
      if (reason != SPDK_NVME_QPAIR_FAILURE_NONE) {
        SPDLOG_ERROR("get failure when polling qpair, reason: {}, rc: {}",
                     int(reason), rc);
      }
    }
  }
  co_return;
}

auto Dev::Attach(const DevConfig &config) -> Status {
  //* parse config
  auto formatted_config = config.Format();
  if (auto rc = spdk_nvme_transport_id_parse(&trid_, formatted_config.c_str());
      rc != 0) {
    SPDLOG_ERROR("fail to parse nvme transport id, rc: {}", rc);
    return Status::InvalidArgs();
  }
  SPDLOG_INFO("formatted config: {}", formatted_config);

  //* probe device
  auto probe_cb = [](void *, const spdk_nvme_transport_id *trid,
                     spdk_nvme_ctrlr_opts *) -> bool {
    SPDLOG_INFO("attaching to {}", std::string_view(trid->traddr));
    return true;
  };
  auto attach_cb = [](void *ctx, const spdk_nvme_transport_id *trid,
                      spdk_nvme_ctrlr *ctrlr,
                      const spdk_nvme_ctrlr_opts *) -> void {
    ((Dev *)ctx)->ctrlr_ = ctrlr;
    SPDLOG_INFO("attached to {}", std::string_view(trid->traddr));
  };
  if (auto rc = spdk_nvme_probe(&trid_, this, probe_cb, attach_cb, nullptr);
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
                 std::string_view(trid_.traddr));
    spdk_nvme_detach(ctrlr_);
    return Status::DeviceFailure();
  }

  //* allocate qpair
  spdk_nvme_io_qpair_opts opts{};
  spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr_, &opts, sizeof(opts));
  opts.delay_cmd_submit = true;  // batch submit
  max_depth_ = opts.io_queue_requests - 1;
  qpair_ = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr_, &opts, sizeof(opts));
  if (qpair_ == nullptr) {
    SPDLOG_ERROR("fail to allocate qpair in {}, detach",
                 std::string_view(trid_.traddr));
    spdk_nvme_detach(ctrlr_);
    return Status::DeviceFailure();
  }

  //* admin poller
  admin_poller_running_ = true;
  asio::co_spawn(coro_ctx, PollAdminQueue(), asio::detached);

  //* io poller
  io_poller_running_ = true;
  asio::co_spawn(coro_ctx, PollIoQueue(), asio::detached);

  return Status::OK();
}

auto Dev::Detach() -> Status {
  spdk_nvme_ctrlr_disconnect_io_qpair(qpair_);
  spdk_nvme_ctrlr_free_io_qpair(qpair_);
  spdk_nvme_detach(ctrlr_);
  return Status::OK();
}

// static auto AsyncSpdkCmd(IO *io) {
//   class SpdkAwaitableCmd {
//     static auto IoDone(void *ctx, const spdk_nvme_cpl *cpl) -> void {
//       auto io = (IO *)ctx;
//       if (spdk_nvme_cpl_is_error(cpl)) {
//         SPDLOG_ERROR("nvme io error status: {}",
//                      spdk_nvme_cpl_get_status_string(&cpl->status));
//       }
//       io->done.store(true);
//     }

//    public:
//     SpdkAwaitableCmd(IO *io) : io_(io) {}

//    public:
//     auto await_ready() -> bool {
//       // submit
//       int rc = 0;
//       switch (io_->type) {
//         case IO::Type::Read: {
//           rc =
//               spdk_nvme_ns_cmd_read(io_->ns, io_->qpair, io_->payload,
//               io_->lba,
//                                     io_->lba_count, IoDone, io_,
//                                     io_->io_flags);
//           break;
//         }
//         case IO::Type::Write: {
//           rc = spdk_nvme_ns_cmd_write(io_->ns, io_->qpair, io_->payload,
//                                       io_->lba, io_->lba_count, IoDone, io_,
//                                       io_->io_flags);
//           break;
//         }
//       }

//       // may be wrong
//       if (rc < 0) {
//         // return error code
//         if (rc == -EINVAL) {
//           io_->status = Status::InvalidArgs();
//         } else if (rc == -ENOMEM) {
//           io_->status = Status::IoQueueFull();
//         } else if (rc == -ENXIO) {
//           io_->status = Status::DeviceFailure();
//         }
//         io_->done.store(true);
//         return true;
//       }

//       // normal case
//       return false;
//     }

//     auto await_suspend(std::coroutine_handle<> h) -> bool {
//       return not io_->done.load(std::memory_order_relaxed);
//     }

//     auto await_resume() -> Status { return io_->status; }

//    private:
//     IO *io_;
//   };

//   return SpdkAwaitableCmd{io};
// }

auto Dev::Read(void *buffer, uint64_t start_lba, uint32_t n_lba)
    -> asio::awaitable<Status> {
  // using fn_t = Status (*)(void *, uint64_t, uint32_t);
  asio::async_compose()
}

}  // namespace driver