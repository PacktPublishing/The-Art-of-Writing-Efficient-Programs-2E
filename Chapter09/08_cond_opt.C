#include <vector>
#include <cstdlib>

#include "benchmark/benchmark.h"


[[gnu::noinline]] void f(bool b, unsigned long x, unsigned long& s) {
    s += b*x;
}

void BM_opt(benchmark::State& state) {
    const unsigned int N = state.range(0);
    std::vector<unsigned long> x(N);
    std::vector<char> b(N);
    for (size_t i = 0; i != N; ++i) {
        x[i] = rand();
        b[i] = rand() & 1;
    }
    for (auto _ : state) {
        unsigned long s = 0;
        for (size_t i = 0; i != N; ++i) f(b[i], x[i], s);
        benchmark::DoNotOptimize(s);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

BENCHMARK(BM_opt)->Arg(1<<20);

BENCHMARK_MAIN();

