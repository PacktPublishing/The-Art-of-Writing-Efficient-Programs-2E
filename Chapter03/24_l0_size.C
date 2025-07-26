#include <cstdlib>

#include "benchmark/benchmark.h"

volatile long a = 0;

void BM_long(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        // Loop is 66 bytes on x86
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4;
            a += 5; a += 6; a += 7; a += 8;
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*8*state.iterations());
}

void BM_short(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        // Loop is 50 bytes on x86
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4;
            a += 5; 
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*5*state.iterations());
}

#define ARGS \
    ->Arg(1<<22)

BENCHMARK(BM_long) ARGS;
BENCHMARK(BM_short) ARGS;

BENCHMARK_MAIN();

