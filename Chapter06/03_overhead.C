#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <iostream>
#include <math.h>

#include "benchmark/benchmark.h"

using namespace std;

std::atomic<unsigned long> xa(0);

void BM_atomic(benchmark::State& state) {
  const size_t nshared = state.range(0);
  const size_t nwork = state.range(1);
  std::atomic<unsigned long>& x = xa;
  double y = 1;
  unsigned long n = 0;
  for (auto _ : state) {
    for (size_t i = 0; i != nshared; ++i) {
      benchmark::DoNotOptimize(x.fetch_add(1, std::memory_order_relaxed));
    }
    for (size_t i = 0; i != nwork; ++i) {
      benchmark::DoNotOptimize(y = sin(++n + cos(y)));
    }
  }
  state.SetItemsProcessed(n);
}

void BM_cas(benchmark::State& state) {
  const size_t nshared = state.range(0);
  const size_t nwork = state.range(1);
  std::atomic<unsigned long>& x = xa;
  double y = 1;
  unsigned long n = 0;
  for (auto _ : state) {
    for (size_t i = 0; i != nshared; ++i) {
      unsigned long xl = x.load(std::memory_order_relaxed);
      while (!x.compare_exchange_strong(xl, xl + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {}
      benchmark::DoNotOptimize(xl);
    }
    for (size_t i = 0; i != nwork; ++i) {
      benchmark::DoNotOptimize(y = sin(++n + cos(y)));
    }
  }
  state.SetItemsProcessed(n);
}

unsigned long x = 0;
std::mutex M;

void BM_mutex(benchmark::State& state) {
  const size_t nshared = state.range(0);
  const size_t nwork = state.range(1);
  if (state.thread_index() == 0) x = 0;
  double y = 1;
  unsigned long n = 0;
  for (auto _ : state) {
    for (size_t i = 0; i != nshared; ++i) {
      std::lock_guard L(M);
      benchmark::DoNotOptimize(++x);
    }
    for (size_t i = 0; i != nwork; ++i) {
      benchmark::DoNotOptimize(y = sin(++n + cos(y)));
    }
  }
  state.SetItemsProcessed(n);
}

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

Spinlock S;

void BM_spinlock(benchmark::State& state) {
  const size_t nshared = state.range(0);
  const size_t nwork = state.range(1);
  if (state.thread_index() == 0) x = 0;
  double y = 1;
  unsigned long n = 0;
  for (auto _ : state) {
    for (size_t i = 0; i != nshared; ++i) {
      std::lock_guard L(S);
      benchmark::DoNotOptimize(++x);
    }
    for (size_t i = 0; i != nwork; ++i) {
      benchmark::DoNotOptimize(y = sin(++n + cos(y)));
    }
  }
  state.SetItemsProcessed(n);
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->Threads(numcpu) \
  ->Args({1, 0}) \
  ->Args({0, 1}) \
  ->Args({1, 1}) \
  ->Args({1, 10}) \
  ->Args({1, 100}) \
  ->Args({1, 1000}) \
  ->Args({1, 10000}) \
  ->Args({10, 1}) \
  ->Args({100, 1}) \
  ->UseRealTime()

BENCHMARK(BM_atomic) ARGS;
BENCHMARK(BM_cas) ARGS;
BENCHMARK(BM_mutex) ARGS;
BENCHMARK(BM_spinlock) ARGS;

BENCHMARK_MAIN();
