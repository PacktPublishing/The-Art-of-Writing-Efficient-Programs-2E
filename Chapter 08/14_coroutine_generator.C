#include <coroutine>
#include <iostream>

template <typename T> struct generator {
  struct promise_type {
    T value_ = -1;

    generator get_return_object() {
      using handle_type=std::coroutine_handle<promise_type>;
      return generator{handle_type::from_promise(*this)};
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void unhandled_exception() {}
    std::suspend_always yield_value(T value) {
      std::cout << "suspend " << value << " was " << value_ << std::endl;
      value_ = value;
      return {};
    }
    void return_void() {}
  };

  std::coroutine_handle<promise_type> h_;
};

generator<int> coro()
{
  int n1, n2;
  co_yield (n1 = 1);
  co_yield (n2 = 1);
  while (true) {
    const int n = n1 + n2;
    n1 = n2;
    n2 = n;
    co_yield n;
  }
}

int main()
{
  std::cout << "Main() started" << std::endl;
  auto h = coro().h_;
  std::cout << "Main() started coro()" << std::endl;
  auto &promise = h.promise();
  for (int i = 0; i < 10; ++i) {
    std::cout << "Main() counter: " << promise.value_ << std::endl;
    h();
  }
  std::cout << "Main() destroys coro()" << std::endl;
  h.destroy();
  std::cout << "Main() done" << std::endl;
}
