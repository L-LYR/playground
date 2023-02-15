#include <spdlog/spdlog.h>

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>

#include "async_event.hh"

using namespace std::chrono_literals;

std::atomic_bool g_running{false};

auto SayHi(boost::asio::io_context& ctx, driver::AsyncEvent& e, uint32_t id)
    -> boost::asio::awaitable<void> {
  boost::asio::steady_timer timer(ctx, 1s);
  while (g_running.load(std::memory_order_relaxed)) {
    spdlog::info("Hi from coro {} at {}", id,
                 std::chrono::steady_clock::now().time_since_epoch().count());
    co_await e.OnHold();
    co_await timer.async_wait(boost::asio::use_awaitable);
  }
  co_return;
}

auto Run(boost::asio::io_context& ctx) -> boost::asio::awaitable<void> {
  g_running = true;
  auto strand = boost::asio::make_strand(ctx);
  driver::AsyncEvent e(ctx, strand);

  e.AsyncNotifyAll();
  for (uint32_t i = 0; i < 2; i++) {
    co_spawn(ctx.get_executor(), SayHi(ctx, e, i), boost::asio::detached);
  }

  co_await boost::asio::steady_timer(ctx, 2s).async_wait(
      boost::asio::use_awaitable);

  g_running = false;
}

auto main(int argc, char* argv[]) -> int {
  boost::asio::io_context ctx(1);

  // boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
  // signals.async_wait([&](auto, auto) { ctx.stop(); });

  boost::asio::co_spawn(ctx, Run(ctx), boost::asio::detached);

  ctx.run();

  return 0;
}