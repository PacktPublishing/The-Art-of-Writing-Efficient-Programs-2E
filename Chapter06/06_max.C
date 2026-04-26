#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <iostream>
#include <random>

#include "benchmark/benchmark.h"

using namespace std;

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

std::atomic<unsigned long> nmax_a;

void BM_cas_never(benchmark::State& state) {
  if (state.thread_index() == 0) nmax_a = 0;
  mt19937_64 r(state.thread_index());
  volatile unsigned long n = r();
  for (auto _ : state) {
    unsigned long cur = nmax_a.load(std::memory_order_relaxed);
    while (n > cur && !nmax_a.compare_exchange_strong(cur, n, std::memory_order_relaxed, std::memory_order_relaxed)) {}
  }
  benchmark::DoNotOptimize(nmax_a.load());
  state.SetItemsProcessed(state.iterations());
}

void BM_cas_grow(benchmark::State& state) {
  if (state.thread_index() == 0) nmax_a = 0;
  unsigned long n = state.thread_index(), dn = state.threads();
  for (auto _ : state) {
    n += dn;
    unsigned long cur = nmax_a.load(std::memory_order_relaxed);
    while (n > cur && !nmax_a.compare_exchange_strong(cur, n, std::memory_order_relaxed, std::memory_order_relaxed)) {}
  }
  benchmark::DoNotOptimize(nmax_a.load());
  state.SetItemsProcessed(state.iterations());
}

Spinlock S;

unsigned long nmax_u = 0;

void BM_spinlock_never(benchmark::State& state) {
  if (state.thread_index() == 0) nmax_u = 0;
  mt19937_64 r(state.thread_index());
  volatile unsigned long n = r();
  for (auto _ : state) {
    std::lock_guard L(S);
    if (n > nmax_u) nmax_u = n;
  }
  benchmark::DoNotOptimize(nmax_u);
  state.SetItemsProcessed(state.iterations());
}

void BM_spinlock_grow(benchmark::State& state) {
  if (state.thread_index() == 0) nmax_u = 0;
  unsigned long n = state.thread_index(), dn = state.threads();
  for (auto _ : state) {
    n += dn;
    std::lock_guard L(S);
    if (n > nmax_u) nmax_u = n;
  }
  benchmark::DoNotOptimize(nmax_u);
  state.SetItemsProcessed(state.iterations());
}

void BM_spinlock_dclp_never(benchmark::State& state) {
  if (state.thread_index() == 0) nmax_a = 0;
  mt19937_64 r(state.thread_index());
  volatile unsigned long n = r();
  for (auto _ : state) {
    if (n > nmax_a.load(std::memory_order_relaxed)) {
      std::lock_guard L(S);
      if (n > nmax_a.load(std::memory_order_relaxed)) nmax_a.store(n, std::memory_order_relaxed);
    }
  }
  benchmark::DoNotOptimize(nmax_a.load());
  state.SetItemsProcessed(state.iterations());
}

void BM_spinlock_dclp_grow(benchmark::State& state) {
  if (state.thread_index() == 0) nmax_a = 0;
  unsigned long n = state.thread_index(), dn = state.threads();
  for (auto _ : state) {
    n += dn;
    if (n > nmax_a.load(std::memory_order_relaxed)) {
      std::lock_guard L(S);
      if (n > nmax_a.load(std::memory_order_relaxed)) nmax_a.store(n, std::memory_order_relaxed);
    }
  }
  benchmark::DoNotOptimize(nmax_a.load());
  state.SetItemsProcessed(state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->ThreadRange(1, numcpu) \
  ->UseRealTime()

BENCHMARK(BM_cas_never) ARGS;
BENCHMARK(BM_spinlock_never) ARGS;
BENCHMARK(BM_spinlock_dclp_never) ARGS;
BENCHMARK(BM_cas_grow) ARGS;
BENCHMARK(BM_spinlock_grow) ARGS;
BENCHMARK(BM_spinlock_dclp_grow) ARGS;

BENCHMARK_MAIN();
