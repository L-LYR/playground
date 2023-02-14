#define CONTINUATION_TEST

#include <coroutine>
#include <functional>
#include <iostream>
#include <thread>

namespace {
template <typename R = std::nullptr_t>
struct Continuation;

void BuildAsyncChain(auto suspendedCoroutine, auto callingCoroutine) {
  suspendedCoroutine.promise().callingCoroutine_ = callingCoroutine;
  callingCoroutine.promise().sync_ = false;
}

#ifdef CONTINUATION_TEST
static int ContinuationPromiseCount = 0;
#endif

template <typename R>
struct Continuation {
  struct promise_type {
    promise_type() noexcept {
#ifdef CONTINUATION_TEST
      ++ContinuationPromiseCount;
#endif
    }
    ~promise_type() noexcept {
#ifdef CONTINUATION_TEST
      --ContinuationPromiseCount;
#endif
    }
    Continuation<R> get_return_object() {
      return Continuation<R>{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    struct ResumeContinuation {
      ResumeContinuation() noexcept {}
      bool await_ready() const noexcept { return false; }
      void await_suspend(
          std::coroutine_handle<promise_type> thisCoroutine) noexcept {
        auto& promise = thisCoroutine.promise();
        if (promise.callingCoroutine_) promise.callingCoroutine_.resume();
        if (!promise.awaited_) thisCoroutine.destroy();
      }
      void await_resume() noexcept {}
    };
    auto initial_suspend() noexcept { return std::suspend_never{}; }
    auto final_suspend() noexcept { return ResumeContinuation{}; }
    void return_value(R result) { result_ = result; }
    void unhandled_exception() noexcept {
      exception_ = std::current_exception();
    }
    R result_ = {};
    std::coroutine_handle<> callingCoroutine_ = {};
    std::exception_ptr exception_ = {};
    bool sync_ = true;
    bool awaited_ = true;
  };

  Continuation(const Continuation&) = delete;
  Continuation& operator=(const Continuation&) = delete;
  Continuation& operator=(Continuation&&) noexcept = delete;

  Continuation() noexcept = default;
  Continuation(Continuation&& t) noexcept = default;
  explicit Continuation(std::coroutine_handle<promise_type> coroutine)
      : coroutine_(coroutine) {}
  ~Continuation() noexcept {
    if (coroutine_.promise().sync_)
      coroutine_.destroy();
    else
      coroutine_.promise().awaited_ = false;
  }

  bool await_ready() const noexcept { return false; }
  void await_suspend(auto callingCoroutine) noexcept {
    if (coroutine_.promise().sync_)
      callingCoroutine.resume();
    else
      BuildAsyncChain(this->coroutine_, callingCoroutine);
  }
  auto await_resume() {
    if (auto exception = coroutine_.promise().exception_)
      std::rethrow_exception(exception);
    auto result = coroutine_.promise().result_;
    if (!coroutine_.promise().awaited_) coroutine_.destroy();
    return result;
  }

  std::coroutine_handle<promise_type> coroutine_;
};

template <typename R>
using AsyncApiCallback = std::function<void(R)>;
template <typename R>
using AsyncApi = std::function<void(AsyncApiCallback<R>)>;
template <typename R>
struct AsyncCallbackContinuationAwaiter {
  bool await_ready() { return false; }
  void await_suspend(auto callingContinuation) {
    callingContinuation.promise().sync_ = false;
    api_([this, callingContinuation](const R& r) mutable {
      result_ = r;
      callingContinuation.resume();
    });
  }
  R await_resume() { return result_; }

  const AsyncApi<R> api_;
  R result_ = {};
};
template <typename R>
auto AsyncCallbackContinuation(AsyncApi<R> api) {
  return AsyncCallbackContinuationAwaiter<R>{api};
}

// TEST
#define BOOST_TEST(x) \
  if (!(x)) std::cout << "Error: " << #x << std::endl;

#define BOOST_TEST_MESSAGE(x) std::cout << x << std::endl;

namespace Fixture {
std::thread t;
bool continuationsRun = false;

void apiAsync(const std::function<void(int)>& callback) {
  t = std::thread([=] {
    std::this_thread::sleep_for(std::chrono::seconds{1});
    callback(41);
  });
}

int apiSync() { return 41; }

Continuation<int> Test1Async() {
  int initalValue = 1;
  BOOST_TEST_MESSAGE("Start Test1Async");
  int x = co_await AsyncCallbackContinuation<int>(&apiAsync);
  x += initalValue;
  BOOST_TEST(x == 42);
  BOOST_TEST_MESSAGE("Test1Async");
  co_return x += 1;
}
Continuation<int> Test1Sync() {
  int initalValue = 1;
  BOOST_TEST_MESSAGE("Start Test1Sync");
  int x = 41;
  x += initalValue;
  BOOST_TEST(x == 42);
  BOOST_TEST_MESSAGE("Test1Sync");
  co_return x += 1;
}
Continuation<int> Test1SyncWithException() {
  BOOST_TEST_MESSAGE("Start Test1SyncWithException");
  throw std::runtime_error("TestException");
  co_return 42;
}

Continuation<int> Test1AsyncWithException() {
  BOOST_TEST_MESSAGE("Start Test1AsyncWithException");
  int x = co_await AsyncCallbackContinuation<int>(&apiAsync);
  throw std::runtime_error("TestException");
  co_return 42;
}

Continuation<double> Test2(auto&& test1) {
  BOOST_TEST_MESSAGE("Start Test2");
  auto x = co_await test1();
  BOOST_TEST(x == 43);
  BOOST_TEST_MESSAGE("Test2");
  co_return x + 1.0;
}

Continuation<double> Test3(auto&& callbackContinuation) {
  BOOST_TEST_MESSAGE("Start Test3");
  auto x = co_await Test2(callbackContinuation);
  BOOST_TEST(x == 44.0);
  BOOST_TEST_MESSAGE("Test3");
  co_return x + 1.0;
}

Continuation<> Test4(auto&& callbackContinuation) {
  BOOST_TEST_MESSAGE("Start Test4");
  auto x = co_await Test3(callbackContinuation);
  BOOST_TEST(x == 45.0);
  BOOST_TEST_MESSAGE("Test4");
  continuationsRun = true;
  co_return {};
}

Continuation<> Test5Catched(auto&& throws) {
  BOOST_TEST_MESSAGE("Start Test5Catched");

  continuationsRun = false;
  try {
    co_await Test4(throws);
    BOOST_TEST(false);
  } catch (std::runtime_error& e) {
    BOOST_TEST(e.what() == std::string("TestException"));
  }
  BOOST_TEST(!continuationsRun);
  co_return {};
}
}  // namespace Fixture
}  // namespace

int main() {
  BOOST_TEST_MESSAGE("main start");
  {
    BOOST_TEST_MESSAGE("Test1Sync start");
    { auto x = Fixture::Test1Sync(); }
    BOOST_TEST_MESSAGE(ContinuationPromiseCount);
    BOOST_TEST(ContinuationPromiseCount == 0);

    BOOST_TEST_MESSAGE("synchron start");
    {
      Fixture::continuationsRun = false;
      Fixture::Test4(&Fixture::Test1Sync);
      BOOST_TEST(Fixture::continuationsRun);
    }
    BOOST_TEST_MESSAGE(ContinuationPromiseCount);
    BOOST_TEST(ContinuationPromiseCount == 0);

    BOOST_TEST_MESSAGE("synchron start with exception");
    { Fixture::Test5Catched(&Fixture::Test1SyncWithException); }
    BOOST_TEST_MESSAGE(ContinuationPromiseCount);
    BOOST_TEST(ContinuationPromiseCount == 0);

    BOOST_TEST_MESSAGE("asynchron simple start with exception");
    {
      []() -> Continuation<> {
        BOOST_TEST_MESSAGE("asynchron simple start with exception Catched");
        try {
          co_await Fixture::Test1AsyncWithException();
          BOOST_TEST(false);
        } catch (std::runtime_error& e) {
          BOOST_TEST(e.what() == std::string("TestException"));
        }
        co_return {};
      }();
      Fixture::t.join();
    }

    BOOST_TEST_MESSAGE("asynchron start with exception");
    {
      Fixture::Test5Catched(&Fixture::Test1AsyncWithException);
      Fixture::t.join();
    }
    BOOST_TEST_MESSAGE(ContinuationPromiseCount);
    BOOST_TEST(ContinuationPromiseCount == 0);

    BOOST_TEST_MESSAGE("asynchron start");
    {
      Fixture::continuationsRun = false;
      Fixture::Test4(&Fixture::Test1Async);
      BOOST_TEST(!Fixture::continuationsRun);
    }
    BOOST_TEST(!Fixture::continuationsRun);
    BOOST_TEST_MESSAGE("main after Test4");
    // std::this_thread::sleep_for( std::chrono::seconds{ 1 } );
    BOOST_TEST_MESSAGE("main after sleep");
    Fixture::t.join();
    BOOST_TEST(Fixture::continuationsRun);
    BOOST_TEST_MESSAGE("after join");
    BOOST_TEST_MESSAGE(ContinuationPromiseCount);
    BOOST_TEST(ContinuationPromiseCount == 0);
  }
}
