#include "llvm_allocator.hpp"
#include <any>
#include <coroutine>
#include <deque>
#include <exception>
#include <print>
#include <utility>
#include <vector>

namespace coroutine {
struct task {

  struct tscheduler {
    std::deque<std::coroutine_handle<>> ready;

    void schedule(std::coroutine_handle<> h) { ready.push_back(h); }

    void iteration() {
      if (ready.empty())
        return;

      auto h = ready.front();
      ready.pop_front();
      if (!h.done())
        h.resume();
    }

    void run() {
      while (!ready.empty()) {
        iteration();
      }
    }
  };

  struct tctx {
    llvm_allocator state_allocator;
    tscheduler scheduler;
  };

  struct promise_type {
    tctx& ctx;
    std::any state;

    std::vector<std::coroutine_handle<>> dependents; // coroutines waiting on me
    std::coroutine_handle<> dependency;              // the one I await on

    promise_type(tctx& ctx_, std::any state_) : ctx(ctx_), state(state_) {}

    task get_return_object() noexcept {
      return task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    auto final_suspend() noexcept {
      struct final_awaitable {
        bool await_ready() noexcept { return false; }

        void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
          auto& promise = h.promise();

          for (auto coros : promise.dependents)
            promise.ctx.scheduler.schedule(coros);
          promise.dependents.clear();
        }

        void await_resume() noexcept {}
      };
      return final_awaitable{};
    }

    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }
  };

  std::coroutine_handle<promise_type> handle;

  explicit task(std::coroutine_handle<promise_type> h) noexcept : handle(h) {}
  task(task&& other) noexcept : handle(std::exchange(other.handle, {})) {}
  ~task() {
    if (handle)
      handle.destroy();
  }

  bool await_ready() const noexcept { return !handle || handle.done(); }

  void await_suspend(std::coroutine_handle<> awaiting) noexcept {
    auto& me = handle.promise(); // the coroutine being awaited
    auto& waiter =
        std::coroutine_handle<promise_type>::from_address(awaiting.address())
            .promise();

    me.dependents.push_back(awaiting); // waiter depends on me
    waiter.dependency = handle;        // I'm the dependency of waiter
  }

  void await_resume() const noexcept {}

  void start() noexcept {
    if (handle && !handle.done())
      handle.resume();
  }

  bool done() const noexcept { return !handle || handle.done(); }
};
} // namespace coroutine

auto C_generator(coroutine::task::tctx& ctx, std::any state)
    -> coroutine::task {
  std::println("Begin: {}", __PRETTY_FUNCTION__);
  std::println("End: {}", __PRETTY_FUNCTION__);
  co_return;
}
auto B_generator(coroutine::task::tctx& ctx, std::any state)
    -> coroutine::task {
  std::println("Begin: {}", __PRETTY_FUNCTION__);
  auto dep = std::any_cast<coroutine::task*>(state);
  co_await *dep;
  std::println("End: {}", __PRETTY_FUNCTION__);
  co_return;
}

auto A_generator(coroutine::task::tctx& ctx, std::any state)
    -> coroutine::task {
  std::println("Begin: {}", __PRETTY_FUNCTION__);
  auto dep = std::any_cast<coroutine::task*>(state);
  co_await *dep;
  std::println("End: {}", __PRETTY_FUNCTION__);

  co_return;
}

int main() {
  coroutine::task::tctx ctx;

  auto C_task = C_generator(ctx, nullptr);
  auto B_task = B_generator(ctx, &C_task);
  auto A_task = A_generator(ctx, &B_task);

  ctx.scheduler.schedule(A_task.handle);
  ctx.scheduler.schedule(B_task.handle);
  ctx.scheduler.schedule(C_task.handle);

  ctx.scheduler.run();
}
