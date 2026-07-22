#include <atomic>
#include <unistd.h>
#include <mutex>

#include "benchmark/benchmark.h"

std::atomic<unsigned long>* p(new std::atomic<unsigned long>);
std::mutex M;

void BM_count(benchmark::State& state) {
  if (state.thread_index() == 0) *p = 0;
  constexpr size_t N = 1000000;
  for (auto _ : state) {
    unsigned long x = 0;
    for (size_t i = 0; i < N; ++i) benchmark::DoNotOptimize(++x);
    std::lock_guard<std::mutex> L(M);
    *p += x;
  }
  state.SetItemsProcessed(state.iterations()*N);
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->ThreadRange(1, numcpu) \
  ->UseRealTime()

BENCHMARK(BM_count) ARGS;

BENCHMARK_MAIN();
