#pragma once

#include <fmt/format.h>
#include <spdk/nvme.h>

#include <boost/asio.hpp>
#include <memory>
#include <string>

#include "status.hh"

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

class Dev {
  class IO {
   public:
    enum class Type : uint8_t {
      Read,
      Write,
    };

   public:
    IO(Type type, Dev *dev, uint64_t start_lba, uint32_t n_lba, void *buffer,
       uint32_t io_flags)
        : type(type),
          dev(dev),
          payload(buffer),
          lba(start_lba),
          lba_count(n_lba),
          io_flags(io_flags) {}

    auto Issue(spdk_nvme_cmd_cb cb, void *cb_arg) -> void;

    auto GetStatus() -> Status { return status; }

   public:
    Type type;
    Dev *dev{nullptr};
    // struct spdk_nvme_ns *ns;
    // struct spdk_nvme_qpair *qpair;
    void *payload;
    uint64_t lba;
    uint32_t lba_count;
    uint32_t io_flags;
    Status status{Status::OK()};
  };

 public:
  Dev(boost::asio::io_context &ctx);
  ~Dev();

  Dev(Dev &) = delete;
  Dev(Dev &&) = delete;
  auto operator=(Dev &) -> Dev = delete;
  auto operator=(Dev &&) -> Dev = delete;

 public:
  auto Attach(const DevConfig &config) -> Status;
  auto Detach() -> Status;

 public:
  auto Read(void *buf, uint64_t start_lba, uint32_t n_lba, uint32_t flags)
      -> boost::asio::awaitable<Status>;
  auto Write(void *buf, uint64_t start_lba, uint32_t n_lba, uint32_t flags)
      -> boost::asio::awaitable<Status>;

 public:
  auto GetSectorSize() -> uint32_t { return sector_size_; }

 private:
  auto PollAdminQueue() -> boost::asio::awaitable<void>;
  auto PollIoQueue() -> boost::asio::awaitable<void>;

 private:
  spdk_nvme_transport_id *trid_{};
  spdk_nvme_ctrlr *ctrlr_{nullptr};
  spdk_nvme_ns *ns_{nullptr};
  spdk_nvme_qpair *qpair_{nullptr};

  uint64_t n_sector_{0};
  uint32_t sector_size_{0};    // in bytes
  uint32_t max_xfer_size_{0};  // in bytes
  uint32_t max_depth_{0};

  std::atomic_bool admin_poller_running_{false};
  std::atomic_bool io_poller_running_{false};

  boost::asio::io_context &coro_ctx;

 private:
  template <boost::asio::completion_handler_for<void(void)> Handler>
  class SpdkNvmeCmdState {
   public:
    SpdkNvmeCmdState(Handler &&handler, IO *io)
        : io_(io),
          handler_(std::move(handler)),
          work_(boost::asio::make_work_guard(handler_)) {}

   public:
    static auto Create(Handler &&handler, IO *io) -> SpdkNvmeCmdState * {
      struct deleter {
        typename std::allocator_traits<boost::asio::associated_allocator_t<
            Handler, boost::asio::recycling_allocator<void>>>::
            template rebind_alloc<SpdkNvmeCmdState>
                alloc;

        void operator()(SpdkNvmeCmdState *ptr) {
          std::allocator_traits<decltype(alloc)>::deallocate(alloc, ptr, 1);
        }
      } d{boost::asio::get_associated_allocator(
          handler, boost::asio::recycling_allocator<void>())};

      std::unique_ptr<SpdkNvmeCmdState, deleter> uninit_ptr(
          std::allocator_traits<decltype(d.alloc)>::allocate(d.alloc, 1), d);

      SpdkNvmeCmdState *ptr =
          new (uninit_ptr.get()) SpdkNvmeCmdState(std::move(handler), io);

      uninit_ptr.release();
      return ptr;
    }

    static auto Callback(void *ctx, const struct spdk_nvme_cpl *cpl) -> void {
      SpdkNvmeCmdState *self = static_cast<SpdkNvmeCmdState *>(ctx);

      struct deleter {
        typename std::allocator_traits<boost::asio::associated_allocator_t<
            Handler, boost::asio::recycling_allocator<void>>>::
            template rebind_alloc<SpdkNvmeCmdState>
                alloc;

        void operator()(SpdkNvmeCmdState *ptr) {
          std::allocator_traits<decltype(alloc)>::destroy(alloc, ptr);
          std::allocator_traits<decltype(alloc)>::deallocate(alloc, ptr, 1);
        }
      } d{boost::asio::get_associated_allocator(
          self->handler_, boost::asio::recycling_allocator<void>())};

      std::unique_ptr<SpdkNvmeCmdState, deleter> state_ptr(self, d);
      SpdkNvmeCmdState state(std::move(*self));
      state_ptr.reset();

      if (cpl != nullptr and spdk_nvme_cpl_is_error(cpl)) {
        // TODO: log here
        self->io_->status = Status::IoFailure();
      }

      boost::asio::dispatch(
          state.work_.get_executor(),
          boost::asio::bind_allocator(
              d.alloc, [handler = std::move(state.handler_)]() mutable {
                std::move(handler)();
              }));
    }

   private:
    IO *io_;
    Handler handler_;
    boost::asio::executor_work_guard<
        boost::asio::associated_executor_t<Handler>>
        work_;
  };

  template <boost::asio::completion_token_for<void(void)> CompletionToken>
  auto AsyncSpdkNvmeCmd(IO *io, CompletionToken &&token) {
    auto init = [](boost::asio::completion_handler_for<void(void)> auto handler,
                   IO *io) {
      using StateType = SpdkNvmeCmdState<decltype(handler)>;
      io->Issue(&StateType::Callback,
                StateType::Create(std::move(handler), io));
      if (not io->GetStatus().IsOK()) {
        handler();
      }
    };
    return boost::asio::async_initiate<CompletionToken, void(void)>(init, token,
                                                                    io);
  }
};

}  // namespace driver