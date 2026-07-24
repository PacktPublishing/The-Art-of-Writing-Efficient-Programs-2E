#include <unistd.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <execution>

#include "benchmark/benchmark.h"

void BM_sort_seq(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    srand(1);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    std::vector<double> vv(N);
    for (auto _ : state) {
        std::copy(std::execution::seq, v.begin(), v.end(), vv.begin());
        std::sort(std::execution::seq, vv.begin(), vv.end());
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_sort_par(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    srand(1);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    std::vector<double> vv(N);
    for (auto _ : state) {
        std::copy(std::execution::seq, v.begin(), v.end(), vv.begin());
        std::sort(std::execution::par, vv.begin(), vv.end());
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_sort_sorted_seq(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    srand(1);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    std::sort(v.begin(), v.end(), std::less<double>());
    std::vector<double> vv(N);
    for (auto _ : state) {
        std::copy(std::execution::seq, v.begin(), v.end(), vv.begin());
        std::sort(std::execution::seq, vv.begin(), vv.end());
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_sort_sorted_par(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    srand(1);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    std::sort(v.begin(), v.end(), std::less<double>());
    std::vector<double> vv(N);
    for (auto _ : state) {
        std::copy(std::execution::seq, v.begin(), v.end(), vv.begin());
        std::sort(std::execution::par, vv.begin(), vv.end());
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_sort_opposite_seq(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    srand(1);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    std::sort(std::execution::seq, v.begin(), v.end(), std::greater<double>());
    std::vector<double> vv(N);
    for (auto _ : state) {
        std::copy(std::execution::seq, v.begin(), v.end(), vv.begin());
        std::sort(std::execution::seq, vv.begin(), vv.end());
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_sort_opposite_par(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    srand(1);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    std::sort(std::execution::par, v.begin(), v.end(), std::greater<double>());
    std::vector<double> vv(N);
    for (auto _ : state) {
        std::copy(std::execution::seq, v.begin(), v.end(), vv.begin());
        std::sort(std::execution::par, vv.begin(), vv.end());
    }
    state.SetItemsProcessed(N*state.iterations());
}

#define ARG \
    ->Arg(1UL<<10) \
    ->Arg(1UL<<15) \
    ->Arg(1UL<<20) \
    ->UseRealTime()

BENCHMARK(BM_sort_seq) ARG;
BENCHMARK(BM_sort_par) ARG;
BENCHMARK(BM_sort_sorted_seq) ARG;
BENCHMARK(BM_sort_sorted_par) ARG;
BENCHMARK(BM_sort_opposite_seq) ARG;
BENCHMARK(BM_sort_opposite_par) ARG;

BENCHMARK_MAIN();
