#include <cstdlib>
#include <vector>

#include "benchmark/benchmark.h"

__attribute__((noinline)) unsigned long f1(unsigned long p1, unsigned long p2) { return p1 - p2; }
__attribute__((noinline)) unsigned long f2(unsigned long p1, unsigned long p2) { return p1 * p2; }

inline unsigned long g1(unsigned long p1, unsigned long p2) { return p1 - p2; }
inline unsigned long g2(unsigned long p1, unsigned long p2) { return p1 * p2; }

void BM_branched(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            if (b1[i]) {
                a1 += f1(p1[i], p2[i]);
            } else {
                a1 += f2(p1[i], p2[i]);
            }
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_branchless(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    decltype(f1)* f[] = { &f2, &f1 };
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            a1 += f[b1[i]](p1[i], p2[i]);
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_branched_predicted(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            if (b1[i]) {
                a1 += f1(p1[i], p2[i]);
            } else {
                a1 += f2(p1[i], p2[i]);
            }
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_branchless_predicted(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    decltype(f1)* f[] = { &f2, &f1 };
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            a1 += f[b1[i]](p1[i], p2[i]);
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_branched_inline(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            if (b1[i]) {
                a1 += g1(p1[i], p2[i]);
            } else {
                a1 += g2(p1[i], p2[i]);
            }
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_branchless_inline(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    decltype(g1)* f[] = { &g2, &g1 };
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            a1 += f[b1[i]](p1[i], p2[i]);
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

#define ARGS \
    ->Arg(1<<22)

BENCHMARK(BM_branched) ARGS;
BENCHMARK(BM_branchless) ARGS;

BENCHMARK(BM_branched_predicted) ARGS;
BENCHMARK(BM_branchless_predicted) ARGS;

BENCHMARK(BM_branched_inline) ARGS;
BENCHMARK(BM_branchless_inline) ARGS;

BENCHMARK_MAIN();

