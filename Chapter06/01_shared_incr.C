#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <iostream>

#include "benchmark/benchmark.h"

using namespace std;

std::atomic<unsigned long> xa(0);

void BM_atomic(benchmark::State& state) {
  std::atomic<unsigned long>& x = xa;
  for (auto _ : state) {
    benchmark::DoNotOptimize(x.fetch_add(1, std::memory_order_relaxed));
  }
  state.SetItemsProcessed(state.iterations());
}

unsigned long x = 0;
std::mutex M;

void BM_mutex(benchmark::State& state) {
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    std::lock_guard L(M);
    benchmark::DoNotOptimize(++x);
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_cas_strong(benchmark::State& state) {
  std::atomic<unsigned long>& x = xa;
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    unsigned long xl = x.load(std::memory_order_relaxed);
    while (!x.compare_exchange_strong(xl, xl + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {}
  }
  state.SetItemsProcessed(state.iterations());
}

void BM_cas_weak(benchmark::State& state) {
  std::atomic<unsigned long>& x = xa;
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    unsigned long xl = x.load(std::memory_order_relaxed);
    while (!x.compare_exchange_weak(xl, xl + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {}
  }
  state.SetItemsProcessed(state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->ThreadRange(1, numcpu) \
  ->UseRealTime()

BENCHMARK(BM_atomic) ARGS;
BENCHMARK(BM_mutex) ARGS;
BENCHMARK(BM_cas_strong) ARGS;
BENCHMARK(BM_cas_weak) ARGS;

BENCHMARK_MAIN();
