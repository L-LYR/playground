#include <spdlog/spdlog.h>

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>

#include "common/async_event.hh"

using namespace std::chrono_literals;
namespace asio = boost::asio;

std::atomic_bool g_running{false};

auto SayHi(asio::io_context& ctx, common::AsyncEvent& e, uint32_t id)
    -> asio::awaitable<void> {
  asio::steady_timer timer(ctx, 1s);
  while (g_running.load(std::memory_order_relaxed)) {
    spdlog::info("Hi from coro {} at {}", id,
                 std::chrono::steady_clock::now().time_since_epoch().count());
    e.AsyncWait(
        [](const boost::system::error_code&) -> void { spdlog::info("?"); });
    co_await timer.async_wait(asio::use_awaitable);
  }
  co_return;
}

auto Run(boost::asio::io_context& ctx) -> asio::awaitable<void> {
  g_running = true;
  auto strand = asio::make_strand(ctx);
  common::AsyncEvent e(ctx, strand);

  for (uint32_t i = 0; i < 10; i++) {
    co_spawn(ctx.get_executor(), SayHi(ctx, e, i), asio::detached);
  }

  co_await asio::steady_timer(ctx, 10s).async_wait(asio::use_awaitable);
  e.AsyncNotifyAll();

  g_running = false;
}

auto main(int argc, char* argv[]) -> int {
  asio::io_context ctx(1);

  // boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
  // signals.async_wait([&](auto, auto) { ctx.stop(); });

  asio::co_spawn(ctx, Run(ctx), asio::detached);

  ctx.run();

  return 0;
}