#include <coroutine>

#include "benchmark/benchmark.h"

#ifdef NOINLINE
#define ATTR __attribute__((noinline))
#else
#define ATTR
#endif // NOINLINE

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
      value_ = value;
      return {};
    }
    void return_void() {}
  };

  std::coroutine_handle<promise_type> h_;
};

ATTR generator<int> coro()
{
  int n1, n2;
  co_yield (n1 = 1);
  co_yield (n2 = 1);
  for (int i = 0;; ++i) {
    const int n = n1 + n2;
    n1 = n2;
    n2 = n;
    co_yield i;
  }
}

void BM_coro(benchmark::State& state) {
  auto h = coro().h_;
  auto &promise = h.promise();
  for (auto _ : state) {
    benchmark::DoNotOptimize(promise.value_);
    h();
  }
  h.destroy();
  state.SetItemsProcessed(state.iterations());
}

class Generator {
  int n1_ = 0;
  int n2_ = 0;
  public:
  ATTR int operator()() { 
    if (n1_ == 0) return (n1_ = 1);
    if (n2_ == 0) return (n2_ = 1);
    const int n = n1_ + n2_;
    n1_ = n2_;
    n2_ = n;
    return n;
  }
};

void BM_generator(benchmark::State& state) {
  Generator g;
  for (auto _ : state) {
    benchmark::DoNotOptimize(g());
  }
  state.SetItemsProcessed(state.iterations());
}

#define ARG \
    ->UseRealTime()

BENCHMARK(BM_coro) ARG;
BENCHMARK(BM_generator) ARG;

BENCHMARK_MAIN();
