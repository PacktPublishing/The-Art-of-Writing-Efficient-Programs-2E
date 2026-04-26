#include <unistd.h>
#include <atomic>
#include <memory>
#include <mutex>

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

class Spinlock {
  public:
  Spinlock() = default;
  Spinlock(const Spinlock&) = delete;
  Spinlock operator=(const Spinlock&) = delete;
  void lock() {
    static const timespec ns = { 0, 1 };
    for (int i = 0; flag_.load(std::memory_order_relaxed) || flag_.exchange(1, std::memory_order_acquire); ++i) {
      if (i == 8) {
        i = 0;
        nanosleep(&ns, nullptr);
      }
    }
  }
  void unlock() { flag_.store(0, std::memory_order_release); }
  private:
  std::atomic<unsigned int> flag_ {};
};

template <typename T>
class ts_shared_ptr {
    public:
    ts_shared_ptr() = default;
    explicit ts_shared_ptr(T* p) : p_(std::shared_ptr<T>(p)) {}
    ts_shared_ptr(const ts_shared_ptr&) = delete;
    ts_shared_ptr& operator=(const ts_shared_ptr&) = delete;

    ts_shared_ptr(ts_shared_ptr&& rhs) noexcept
        : p_([&] { std::lock_guard lk(rhs.s_); auto tmp = std::move(rhs.p_); return tmp; }())
        {}

    ts_shared_ptr& operator=(ts_shared_ptr&& rhs) noexcept {
        if (this != &rhs) {
            std::scoped_lock lk(s_, rhs.s_);
            p_ = std::move(rhs.p_);
        }
        return *this;
    }

    ~ts_shared_ptr() = default;

    void publish(T* p) noexcept {
        std::lock_guard lk(s_);
        p_.reset(p);
    }

    void publish(std::shared_ptr<T> p) noexcept {
        std::lock_guard lk(s_);
        p_ = std::move(p);
    }

    std::shared_ptr<const T> republish(T* p) noexcept {
        std::lock_guard lk(s_);
        std::shared_ptr<const T> old = std::move(p_);
        p_.reset(p);
        return old;
    }

    std::shared_ptr<const T> republish(std::shared_ptr<T> p) noexcept {
        std::lock_guard lk(s_);
        std::shared_ptr<const T> old = std::move(p_);
        p_ = std::move(p);
        return old;
    }

    std::shared_ptr<const T> get() const noexcept {
        std::lock_guard lk(s_);
        return p_;
    }

    const T& operator*() const noexcept {
        std::lock_guard lk(s_);
        return *p_;
    }

    private:
    mutable Spinlock s_;
    std::shared_ptr<T> p_{};
};

ts_shared_ptr<A> p(new A(42));

void BM_ptr_get(benchmark::State& state) {
  volatile A x;
  for (auto _ : state) {
    REPEAT(benchmark::DoNotOptimize(x = *p.get()););
  }
  state.SetItemsProcessed(32*state.iterations());
}

shared_ptr<A> q(new A(7));

void BM_ptr_publish(benchmark::State& state) {
  p.publish(q);
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

shared_ptr<A> sp(new A(42));

void BM_shared_ptr_deref(benchmark::State& state) {
  volatile A x;
  for (auto _ : state) {
    REPEAT(benchmark::DoNotOptimize(x = *sp););
  }
  state.SetItemsProcessed(32*state.iterations());
}

void BM_shared_ptr_assign(benchmark::State& state) {
  for (auto _ : state) {
    REPEAT(benchmark::DoNotOptimize(sp = q);)
  }
  state.SetItemsProcessed(32*state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->UseRealTime()

BENCHMARK(BM_ptr_get) ARGS;
BENCHMARK(BM_ptr_publish) ARGS;
BENCHMARK(BM_ptr_republish) ARGS;
BENCHMARK(BM_shared_ptr_deref) ARGS;
BENCHMARK(BM_shared_ptr_assign) ARGS;

BENCHMARK_MAIN();
