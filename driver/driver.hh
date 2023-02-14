#pragma once

#include <fmt/format.h>
#include <spdk/nvme.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/noncopyable.hpp>
#include <memory>
#include <string>

#include "status.hh"

namespace asio = boost::asio;

namespace driver {

struct DevConfig {
  bool is_rdma;
  bool is_ipv4;
  uint32_t ns_id;
  std::string addr;
  std::string svc_id;
  std::string subnqn;

  auto Format() const -> std::string;
};

struct IO {
  IO(void *buffer, uint64_t start_lba, uint32_t n_lba)
      : payload(buffer), lba(start_lba), lba_count(n_lba) {}

  enum class Type {
    Read,
    Write,
  };
  std::atomic_bool done{false};
  Type type;
  struct spdk_nvme_ns *ns;
  struct spdk_nvme_qpair *qpair;
  void *payload;
  uint64_t lba;
  uint32_t lba_count;
  uint32_t io_flags;
  Status status{Status::OK()};
};

class Dev : boost::noncopyable {
 public:
  Dev();
  ~Dev();

 public:
  auto Attach(const DevConfig &config) -> Status;
  auto Detach() -> Status;

 public:
  auto Read(void *buffer, uint64_t start_lba, uint32_t n_lba)
      -> asio::awaitable<Status>;

 private:
  auto PollAdminQueue() -> asio::awaitable<void>;
  auto PollIoQueue() -> asio::awaitable<void>;

 private:
  spdk_nvme_ctrlr *ctrlr_{nullptr};
  spdk_nvme_ns *ns_{nullptr};
  spdk_nvme_qpair *qpair_{nullptr};
  spdk_nvme_transport_id trid_{};

  uint64_t n_sector_{0};
  uint32_t sector_size_{0};    // in bytes
  uint32_t max_xfer_size_{0};  // in bytes
  uint32_t max_depth_{0};

  std::atomic_uint32_t cur_depth_{0};

  std::atomic_bool admin_poller_running_{false};
  std::atomic_bool io_poller_running_{false};

  asio::io_context coro_ctx;
  asio::strand<asio::io_context::executor_type> strand_;
};

class SpdkCmd {
  enum class State { Starting, Waiting, Finishing };
  static auto IoDone(void *ctx, const spdk_nvme_cpl *cpl) -> void {
    auto io = (IO *)ctx;
    if (spdk_nvme_cpl_is_error(cpl)) {
      SPDLOG_ERROR("nvme io error status: {}",
                   spdk_nvme_cpl_get_status_string(&cpl->status));
      io->status = Status::IoFailure();
    }
    io->done.store(true);
  }

 public:
  SpdkCmd(IO *io) : io_(io), state_(State::Starting) {}

 public:
  template <typename Self>
  auto operator()(Self &self, ) -> void {
    switch (state_) {
      case State::Starting:
        break;
      case State::Waiting:
        self.complete();
        break;
      case State::Finishing:
        self.complete();
        break;
    }
  }

 private:
  IO *io_;
  State state_;
};

}  // namespace driver