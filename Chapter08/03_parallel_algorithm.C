#include <unistd.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <execution>

#include "benchmark/benchmark.h"

void BM_copy_seq(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    std::vector<double> vv(N);
    for (auto _ : state) {
        std::copy(std::execution::seq, v.begin(), v.end(), vv.begin());
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_copy_par(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    std::vector<double> vv(N);
    for (auto _ : state) {
        std::copy(std::execution::par, v.begin(), v.end(), vv.begin());
    }
    state.SetItemsProcessed(N*state.iterations());
}

#define ARG \
    ->Arg(1UL<<10) \
    ->Arg(1UL<<15) \
    ->Arg(1UL<<20) \
    ->Arg(1UL<<24) \
    ->UseRealTime() 

BENCHMARK(BM_copy_seq) ARG;
BENCHMARK(BM_copy_par) ARG;

BENCHMARK_MAIN();
