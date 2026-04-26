#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>

#include "benchmark/benchmark.h"

#define REPEAT2(x) x x
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT(x) REPEAT32(x)

std::atomic<unsigned long> sum(0);
struct aligned_uint64_t {
    alignas(std::hardware_destructive_interference_size) unsigned long i {0};
};
aligned_uint64_t* a = new aligned_uint64_t[1024];
void BM_incr(benchmark::State& state) {
    auto& x = a[state.thread_index()].i;
    x = 0;
    for (auto _ : state) {
        REPEAT(benchmark::DoNotOptimize(++x););
    }
    sum += x;
    state.SetItemsProcessed(32*state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);
#define ARG \
    ->ThreadRange(1, numcpu) \
    ->UseRealTime()

BENCHMARK(BM_incr) ARG;

BENCHMARK_MAIN();
