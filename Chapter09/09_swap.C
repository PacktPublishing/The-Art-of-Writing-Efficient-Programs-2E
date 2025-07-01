#include <vector>
#include <cstdlib>

#include "benchmark/benchmark.h"


template <typename T>
[[gnu::noinline]] 
void myswap(T* p, T* q) {
    if (p && q) {
        T t = *p;
        *p = *q;
        *q = t;
    }
}

void BM_unchecked_rand(benchmark::State& state) {
    const unsigned int N = state.range(0);
    unsigned long a = 1, b = 2;
    std::vector<unsigned long*> p(N);
    std::vector<unsigned long*> q(N);
    for (size_t i = 0; i != N; ++i) {
        if (rand() & 3) p[i] = &a;
        if (rand() & 3) q[i] = &b;
    }
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) myswap(p[i], q[i]);
        benchmark::DoNotOptimize(a);
        benchmark::DoNotOptimize(b);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_checked_rand(benchmark::State& state) {
    const unsigned int N = state.range(0);
    unsigned long a = 1, b = 2;
    std::vector<unsigned long*> p(N);
    std::vector<unsigned long*> q(N);
    for (size_t i = 0; i != N; ++i) {
        if (rand() & 3) p[i] = &a;
        if (rand() & 3) q[i] = &b;
    }
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) if (p[i] && q[i]) myswap(p[i], q[i]);
        benchmark::DoNotOptimize(a);
        benchmark::DoNotOptimize(b);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_unchecked(benchmark::State& state) {
    const unsigned int N = state.range(0);
    unsigned long a = 1, b = 2;
    std::vector<unsigned long*> p(N);
    std::vector<unsigned long*> q(N);
    for (size_t i = 0; i != N; ++i) {
        if (rand() > 0) p[i] = &a;
        if (rand() > 0) q[i] = &b;
    }
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) myswap(p[i], q[i]);
        benchmark::DoNotOptimize(a);
        benchmark::DoNotOptimize(b);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_checked(benchmark::State& state) {
    const unsigned int N = state.range(0);
    unsigned long a = 1, b = 2;
    std::vector<unsigned long*> p(N);
    std::vector<unsigned long*> q(N);
    for (size_t i = 0; i != N; ++i) {
        if (rand() > 0) p[i] = &a;
        if (rand() > 0) q[i] = &b;
    }
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) if (p[i] && q[i]) myswap(p[i], q[i]);
        benchmark::DoNotOptimize(a);
        benchmark::DoNotOptimize(b);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

BENCHMARK(BM_checked)->Arg(1<<20);
BENCHMARK(BM_unchecked)->Arg(1<<20);
BENCHMARK(BM_checked_rand)->Arg(1<<20);
BENCHMARK(BM_unchecked_rand)->Arg(1<<20);

BENCHMARK_MAIN();

