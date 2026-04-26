#include <unistd.h>
#include <atomic>

#include "benchmark/benchmark.h"

#define REPEAT2(x) {x} {x}
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT64(x) REPEAT32(x) REPEAT32(x)
#define REPEAT(x) REPEAT64(x)

using namespace std;

struct A {
  int i;
  A(int i = 0) : i(i) {}
  A& operator=(const A& rhs) { i = rhs.i; return *this; }
  volatile A& operator=(const A& rhs) volatile { i = rhs.i; return *this; }
};

template <typename T>
class ts_unique_ptr {
    public:
    ts_unique_ptr() = default;
    explicit ts_unique_ptr(T* p) : p_(p) {}
    ts_unique_ptr(const ts_unique_ptr&) = delete;
    ts_unique_ptr& operator=(const ts_unique_ptr&) = delete;
    ts_unique_ptr(ts_unique_ptr&& rhs) noexcept : p_(rhs.p_.load(std::memory_order_relaxed)) {
        rhs.p_.store(nullptr, std::memory_order_relaxed);
    }
    ts_unique_ptr& operator=(ts_unique_ptr&&) = delete;
    ~ts_unique_ptr() { delete p_.load(std::memory_order_relaxed); }
    void publish(T* p) noexcept {
      p_.store(p, std::memory_order_release);
    }
    const T* republish(T* p) noexcept {
      const T* const old_p = this->get();
      p_.store(p, std::memory_order_release);
      return old_p;
    }
    const T* get() const noexcept { return p_.load(std::memory_order_acquire); }
    const T& operator*() const noexcept { return *this->get(); }
    private:
    std::atomic<T*> p_ {};
};

ts_unique_ptr<A> p(new A(42));

void BM_ptr_get(benchmark::State& state) {
  volatile A x;
  for (auto _ : state) {
    REPEAT(benchmark::DoNotOptimize(x = *p.get()););
  }
  state.SetItemsProcessed(32*state.iterations());
}

A* volatile q(new A(7));

void BM_ptr_publish(benchmark::State& state) {
  for (auto _ : state) {
    REPEAT(p.publish(q);)
  }
  state.SetItemsProcessed(32*state.iterations());
}

void BM_ptr_republish(benchmark::State& state) {
  for (auto _ : state) {
    REPEAT(benchmark::DoNotOptimize(p.republish(q));) 
  }
  state.SetItemsProcessed(32*state.iterations());
}

A* rp(new A(42));

void BM_raw_ptr_deref(benchmark::State& state) {
  volatile A x;
  for (auto _ : state) {
    REPEAT(benchmark::DoNotOptimize(x = *rp););
  }
  state.SetItemsProcessed(32*state.iterations());
}

void BM_raw_ptr_assign(benchmark::State& state) {
  for (auto _ : state) {
    REPEAT(benchmark::DoNotOptimize(rp = q);)
  }
  state.SetItemsProcessed(32*state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->UseRealTime()

BENCHMARK(BM_ptr_get) ARGS;
BENCHMARK(BM_ptr_publish) ARGS;
BENCHMARK(BM_ptr_republish) ARGS;
BENCHMARK(BM_raw_ptr_deref) ARGS;
BENCHMARK(BM_raw_ptr_assign) ARGS;

BENCHMARK_MAIN();
