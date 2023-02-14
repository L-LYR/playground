#pragma once

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>

namespace asio = boost::asio;

namespace common {

// reference:
//  https://stackoverflow.com/questions/6775873/boost-asio-asynchronously-waiting-on-a-condition-variable
class AsyncEvent {
 public:
  AsyncEvent(asio::io_context& coro_ctx,
             asio::strand<asio::io_context::executor_type>& strand)
      : strand_(strand),
        deadline_timer_(
            coro_ctx, boost::posix_time::ptime(boost::posix_time::pos_infin)) {}

  // 'handler' must be serialised through the same 'strand_' as 'cancel' or
  // 'cancel_one'
  //  because using 'asio::deadline_timer' from multiple threads is not
  //  thread safe
  template <class WaitHandler>
  void AsyncWait(WaitHandler&& handler) {
    deadline_timer_.async_wait(handler);
  }
  void AsyncNotifyOne() {
    asio::post(strand_, std::bind(&AsyncEvent::AsyncNotifyOneSerialized, this));
  }
  void AsyncNotifyAll() {
    asio::post(strand_, std::bind(&AsyncEvent::AsyncNotifyAllSerialized, this));
  }

 private:
  void AsyncNotifyOneSerialized() { deadline_timer_.cancel_one(); }
  void AsyncNotifyAllSerialized() { deadline_timer_.cancel(); }

 private:
  asio::strand<asio::io_context::executor_type>& strand_;
  asio::deadline_timer deadline_timer_;
};

}  // namespace common