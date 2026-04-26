#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <iostream>

#include "benchmark/benchmark.h"

using namespace std;

class AtomicCount {
    std::atomic<unsigned long> c_ {};
    public:
    unsigned long incr() noexcept { return 1 + c_.fetch_add(1, std::memory_order_relaxed); }
    unsigned long get() const noexcept { return c_.load(std::memory_order_relaxed); }
};

AtomicCount ac;

void BM_atomic_count(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(ac.incr());
  }
  state.SetItemsProcessed(state.iterations());
}

std::atomic<unsigned long> xa(0);

void BM_cas_strong_count(benchmark::State& state) {
  std::atomic<unsigned long>& x = xa;
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    unsigned long xl = x.load(std::memory_order_relaxed);
    while (!x.compare_exchange_strong(xl, xl + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {}
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_cas_weak_count(benchmark::State& state) {
  std::atomic<unsigned long>& x = xa;
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    unsigned long xl = x.load(std::memory_order_relaxed);
    while (!x.compare_exchange_weak(xl, xl + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {}
  }
  state.SetItemsProcessed(state.iterations());
}

class AtomicIndex {
    std::atomic<unsigned long> c_ {};
    public:
    unsigned long incr() noexcept { return 1 + c_.fetch_add(1, std::memory_order_release); }
    unsigned long get() const noexcept { return c_.load(std::memory_order_acquire); }
};

AtomicIndex ai;

void BM_atomic_index(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(ai.incr());
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_cas_strong_index(benchmark::State& state) {
  std::atomic<unsigned long>& x = xa;
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    unsigned long xl = x.load(std::memory_order_relaxed);
    while (!x.compare_exchange_strong(xl, xl + 1, std::memory_order_relaxed, std::memory_order_release)) {}
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_cas_weak_index(benchmark::State& state) {
  std::atomic<unsigned long>& x = xa;
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    unsigned long xl = x.load(std::memory_order_relaxed);
    while (!x.compare_exchange_weak(xl, xl + 1, std::memory_order_relaxed, std::memory_order_release)) {}
  }
  state.SetItemsProcessed(state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->ThreadRange(1, numcpu) \
  ->UseRealTime()

BENCHMARK(BM_atomic_count) ARGS;
BENCHMARK(BM_cas_strong_count) ARGS;
BENCHMARK(BM_cas_weak_count) ARGS;
BENCHMARK(BM_atomic_index) ARGS;
BENCHMARK(BM_cas_strong_index) ARGS;
BENCHMARK(BM_cas_weak_index) ARGS;

BENCHMARK_MAIN();
