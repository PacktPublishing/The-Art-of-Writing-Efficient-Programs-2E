#include <cstdlib>
#include <vector>

#include "benchmark/benchmark.h"

void BM_multiply(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    for (auto _ : state) {
        unsigned long a2 = 0;
        for (size_t i = 0; i < N; ++i) {
            a2 += p1[i] * p2[i];
        }
        benchmark::DoNotOptimize(a2);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

#define ARGS \
    ->Arg(1<<22)

BENCHMARK(BM_multiply) ARGS;

BENCHMARK_MAIN();

