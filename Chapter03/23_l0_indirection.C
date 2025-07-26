#include <vector>
#include <memory>
#include <numeric>
#include <algorithm>
#include <random>

#include "benchmark/benchmark.h"

double result = 0;

struct B {
    virtual void f(double x) = 0;
    virtual ~B() = default;
};

// --- Three different implementations ---
struct D1 : public B {
    void f(double x) override { result += (x + 5) * (x - 3); }
};
struct D2 : public B {
    void f(double x) override { result -= (x + 4) * (x - 2); }
};
struct D3 : public B {
    void f(double x) override { result *= (x + 2) * (x - 1); }
};

void BM_trash_l0(benchmark::State& state) {
    std::vector<std::unique_ptr<B>> v;
    const size_t N = state.range(0);
    constexpr size_t NF = 1UL << 10;
    v.reserve(NF);
    for (size_t i = 0; i != NF/3; ++i) {
        v.push_back(std::make_unique<D1>());
    }
    for (size_t i = NF/3; i != 2*NF/3; ++i) {
        v.push_back(std::make_unique<D2>());
    }
    for (size_t i = 2*NF/3; i != NF; ++i) {
        v.push_back(std::make_unique<D3>());
    }
    std::shuffle(v.begin(), v.end(), std::mt19937{std::random_device{}()});

    for (auto _ : state) {
        // Main loop: This is hostile to the L0 cache
        for (size_t i = 0; i != N; ++i) {
            // The target of this call changes every time, causing L0 misses.
            v[i % NF]->f(i);
        }
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(N*state.iterations());
}

void BM_fit_l0(benchmark::State& state) {
    std::vector<std::unique_ptr<B>> v;
    const size_t N = state.range(0);
    constexpr size_t NF = 1UL << 10;
    v.reserve(NF);
    for (size_t i = 0; i != NF/3; ++i) {
        v.push_back(std::make_unique<D1>());
    }
    for (size_t i = NF/3; i != 2*NF/3; ++i) {
        v.push_back(std::make_unique<D2>());
    }
    for (size_t i = 2*NF/3; i != NF; ++i) {
        v.push_back(std::make_unique<D3>());
    }

    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            v[i % NF]->f(i);
        }
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(N*state.iterations());
}

#define ARGS \
    ->Arg(1<<22)

BENCHMARK(BM_trash_l0) ARGS;
BENCHMARK(BM_fit_l0) ARGS;

BENCHMARK_MAIN();
