#include <spdlog/spdlog.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/signal_set.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "../common/spdk_env.hh"
#include "driver.hh"

using namespace std::chrono_literals;

using namespace driver;

common::SPDKEnv _env_;

auto main(int argc, char* argv[]) -> int {
  DevConfig config{
      .is_rdma = true,
      .is_ipv4 = true,
      .ns_id = 1,
      .addr = "192.168.200.17",
      .svc_id = "4420",
  };

  boost::asio::io_context ctx(1);

  std::thread t;

  Dev dev(ctx);

  if (dev.Attach(config).IsOK()) {
    t = std::thread(([&ctx]() { ctx.run(); }));
  } else {
    return -1;
  }

  boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
  signals.async_wait([&](auto, auto) {
    ctx.stop();
    dev.Detach();
  });

  auto submit_io = [&dev](uint32_t start_lba) -> boost::asio::awaitable<void> {
    auto buf_size = 4096;
    auto n_lba = buf_size / dev.GetSectorSize();
    auto wbuf = (char*)spdk_dma_zmalloc(buf_size, buf_size, nullptr);
    auto rbuf = (char*)spdk_dma_zmalloc(buf_size, buf_size, nullptr);
    auto cur_lba = start_lba;
    auto s = Status::OK();
    long double wsum = 0;
    long double rsum = 0;
    uint32_t n = 10240;
    auto c = 't';
    memset(wbuf, c, buf_size);
    auto tik = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < n; i++) {
      // memset(rbuf, 0, buf_size);
      // auto tik = std::chrono::high_resolution_clock::now();
      // s = co_await dev.Write(wbuf, cur_lba, n_lba, 0);
      // auto tok = std::chrono::high_resolution_clock::now();
      // wsum += (tok - tik).count();

      // if (not s.IsOK()) {
      //   break;
      // }

      s = co_await dev.Read(rbuf, cur_lba, n_lba, 0);

      if (not s.IsOK()) {
        break;
      }
      // if (strncmp(wbuf, rbuf, buf_size) != 0) {
      //   s = Status::InternalError();
      //   break;
      // }
      cur_lba += n_lba;
    }
    auto tok = std::chrono::steady_clock::now();
    rsum += std::chrono::duration_cast<std::chrono::microseconds>(tok - tik)
                .count();
    spdk_dma_free(wbuf);
    spdk_dma_free(rbuf);
    SPDLOG_INFO("result: {}", s.IsOK());
    SPDLOG_INFO("{} us {} MB/s", rsum,
                (buf_size * n) / 1024.0 / 1024.0 / rsum * 1e6);
  };
  auto tik = std::chrono::steady_clock::now();
  std::vector<std::future<void>> fs;
  for (auto i = 0; i < 8; i++) {
    fs.emplace_back(boost::asio::co_spawn(ctx, submit_io(i * 8 * 10240),
                                          boost::asio::use_future));
  }
  for (auto&& f : fs) {
    f.get();
  }
  auto tok = std::chrono::steady_clock::now();
  long double sum =
      std::chrono::duration_cast<std::chrono::microseconds>(tok - tik).count();
  SPDLOG_INFO("{} us {} MB/s", sum,
              16 * (4096 * 10240) / 1024.0 / 1024.0 / sum * 1e6);

  if (t.joinable()) {
    t.join();
  }

  return 0;
}