#include <cstdlib>
#include <vector>

#include "benchmark/benchmark.h"

void BM_false_branch(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N), c2(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
        c2[i] = !c1[i];
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    int* b2 = c2.data();
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            if (bool(b1[i]) || bool(b2[i])) {
                a1 += p1[i];
            } else {
                a1 *= p2[i];
            }
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_false_branch_tmp(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N), c2(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
        c2[i] = !c1[i];
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    int* b2 = c2.data();
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            const bool tmp = bool(b1[i]) || bool(b2[i]);
            if (tmp) {
                a1 += p1[i];
            } else {
                a1 *= p2[i];
            }
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_false_branch_arith(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N), c2(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
        c2[i] = !c1[i];
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    int* b2 = c2.data();
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            if (bool(b1[i]) + bool(b2[i])) {
                a1 += p1[i];
            } else {
                a1 *= p2[i];
            }
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_false_branch_bitwise(benchmark::State& state) {
    srand(1);
    const unsigned int N = state.range(0);
    std::vector<unsigned long> v1(N), v2(N);
    std::vector<int> c1(N), c2(N);
    for (size_t i = 0; i < N; ++i) {
        v1[i] = rand();
        v2[i] = rand();
        c1[i] = rand() & 0x1;
        c2[i] = !c1[i];
    }
    unsigned long* p1 = v1.data();
    unsigned long* p2 = v2.data();
    int* b1 = c1.data();
    int* b2 = c2.data();
    for (auto _ : state) {
        unsigned long a1 = 0;
        for (size_t i = 0; i < N; ++i) {
            if (bool(b1[i]) | bool(b2[i])) {
                a1 += p1[i];
            } else {
                a1 *= p2[i];
            }
        }
        benchmark::DoNotOptimize(a1);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

#define ARGS \
    ->Arg(1<<22)

BENCHMARK(BM_false_branch) ARGS;
BENCHMARK(BM_false_branch_tmp) ARGS;
BENCHMARK(BM_false_branch_arith) ARGS;
BENCHMARK(BM_false_branch_bitwise) ARGS;

BENCHMARK_MAIN();

