#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace driver {

// reference:
//  https://stackoverflow.com/questions/6775873/boost-boost::boost::asio-asynchronously-waiting-on-a-condition-variable
class AsyncEvent {
 public:
  AsyncEvent(
      boost::asio::io_context& coro_ctx,
      boost::asio::strand<boost::asio::io_context::executor_type>& strand)
      : strand_(strand),
        deadline_timer_(
            coro_ctx, boost::posix_time::ptime(boost::posix_time::pos_infin)) {}

  // 'handler' must be serialised through the same 'strand_' as 'cancel' or
  // 'cancel_one' because using 'boost::asio::deadline_timer' from multiple
  // threads is not thread safe
  auto OnHold() -> boost::asio::awaitable<void> {
    co_await deadline_timer_.async_wait(boost::asio::use_awaitable);
  }
  template <class WaitToken>
  auto AsyncWait(WaitToken&& token) -> void {
    deadline_timer_.async_wait(token);
  }
  auto AsyncNotifyOne() -> void {
    boost::asio::post(strand_,
                      std::bind(&AsyncEvent::AsyncNotifyOneSerialized, this));
  }
  auto AsyncNotifyAll() -> void {
    boost::asio::post(strand_,
                      std::bind(&AsyncEvent::AsyncNotifyAllSerialized, this));
  }

 private:
  auto AsyncNotifyOneSerialized() -> void { deadline_timer_.cancel_one(); }
  auto AsyncNotifyAllSerialized() -> void { deadline_timer_.cancel(); }

 private:
  boost::asio::strand<boost::asio::io_context::executor_type>& strand_;
  boost::asio::deadline_timer deadline_timer_;
};

}  // namespace driver