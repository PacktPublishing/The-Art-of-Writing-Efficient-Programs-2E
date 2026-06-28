#include <unistd.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <execution>

#include "benchmark/benchmark.h"

auto work = [](double& x){ x = sin(x) + cos(x)*exp(-x); };

void BM_foreach_seq(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    for (auto _ : state) {
        std::for_each(std::execution::seq, v.begin(), v.end(), work);
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_foreach_par(benchmark::State& state) {
    const size_t N = state.range(0);
    std::vector<double> v(N);
    std::for_each(v.begin(), v.end(), [](double& x){ x = rand(); });
    for (auto _ : state) {
        std::for_each(std::execution::par, v.begin(), v.end(), work);
    }
    state.SetItemsProcessed(N*state.iterations());
}

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

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);
#define ARG \
    ->Arg(1UL<<15) \
    ->Arg(1UL<<20) \
    ->UseRealTime() \
    ->ThreadRange(1, numcpu)

BENCHMARK(BM_foreach_seq) ARG;
BENCHMARK(BM_foreach_par) ARG;
BENCHMARK(BM_sort_seq) ARG;
BENCHMARK(BM_sort_par) ARG;

BENCHMARK_MAIN();
