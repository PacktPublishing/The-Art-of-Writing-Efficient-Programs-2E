#include <cstdlib>
#include <vector>

#include "benchmark/benchmark.h"

void BM_add_multiply_unrolled(benchmark::State& state) {
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
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; i += 16) {
            a1 += p1[i     ] * p2[i     ]
               +  p1[i +  1] * p2[i +  1]
               +  p1[i +  2] * p2[i +  2]
               +  p1[i +  3] * p2[i +  3]
               +  p1[i +  4] * p2[i +  4]
               +  p1[i +  5] * p2[i +  5]
               +  p1[i +  6] * p2[i +  6]
               +  p1[i +  7] * p2[i +  7]
               +  p1[i +  8] * p2[i +  8]
               +  p1[i +  9] * p2[i +  9]
               +  p1[i + 10] * p2[i + 10]
               +  p1[i + 11] * p2[i + 11]
               +  p1[i + 12] * p2[i + 12]
               +  p1[i + 13] * p2[i + 13]
               +  p1[i + 14] * p2[i + 14]
               +  p1[i + 15] * p2[i + 15];
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

#define ARGS \
    ->Arg(1<<22)

BENCHMARK(BM_add_multiply_unrolled) ARGS;

BENCHMARK_MAIN();

