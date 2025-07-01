#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <iostream>
#include <vector>
#include "benchmark/benchmark.h"
using namespace std;

constexpr size_t nr = 1UL << 20;
std::vector<size_t> vr {
    [](size_t nr) { 
        std::vector<size_t> v;
        v.reserve(nr);
        srand(1);
        for (size_t i = 0; i < nr; ++i) v[i] = rand();
        return v;
    }(nr)
};

void BM_make_str_new(benchmark::State& state) {
  const size_t NMax = state.range(0);
  size_t ir = 0;
  for (auto _ : state) {
    const int r = vr[ir++ % nr];
    const size_t N = (r % NMax) + 1;
    char* buf = new char[N];
    memset(buf, 0xab, N);
    if (r < 0) cout << buf;
    delete [] buf;
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations());
}

#define ARG \
  ->Arg(1UL << 10) \
  ->UseRealTime()

BENCHMARK(BM_make_str_new) ARG;

BENCHMARK_MAIN();
