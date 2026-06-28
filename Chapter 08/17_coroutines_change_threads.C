#include <coroutine>
#include <iostream>
#include <thread>
#include <mutex>
#include <map>

class tid_t {
  std::map<std::thread::id, int> ids_;
  int current_id_ {};
  std::mutex m_;

  public:
  int operator()(std::thread::id id) {
    std::lock_guard g(m_);
    if (ids_.contains(id)) return ids_[id];
    else return (ids_[id] = current_id_++);
  }
  int operator()() {
    return (*this)(std::this_thread::get_id());
  }
  int operator()(const std::jthread& t) {
    return (*this)(t.get_id());
  }
} TID;

struct awaitable {
  std::jthread& t3;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) {
    std::jthread& out = t3;
    out = std::jthread([h] { h.resume(); });
    std::cout << "New thread ID: " << TID(out) << std::endl;
  }
  void await_resume() {}
  ~awaitable() {
    std::cout << "Awaitable destroyed on thread " << TID() << " with thread " << TID(t3) << std::endl;
  }
  awaitable(std::jthread& t) : t3(t) {
    std::cout << "Awaitable constructed on thread " << TID() << std::endl;
  }
};

struct task{
  struct promise_type {
    task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

task coro(std::jthread& t1, std::jthread& t2, int i) {
  std::cout << "Coroutine started on thread " << TID() << " i=" << i << std::endl;
  co_await awaitable{t1};
  std::cout << "Coroutine resumed on thread " << TID() << " i=" << i << std::endl;
  co_await awaitable{t2};
  std::cout << "Coroutine done on thread " << TID() << " i=" << i << std::endl;
  // awaiter destroyed here
}

int main() {
  std::cout << "Main thread: " << TID() << std::endl;
  {
    std::jthread t1, t2;
    coro(t1, t2, 42);
    std::cout << "Main thread done: " << TID() << std::endl;
  }
  std::cout << "Main thread really done: " << TID() << std::endl;
}
