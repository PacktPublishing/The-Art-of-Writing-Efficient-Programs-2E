#include <benchmark/benchmark.h>
#include <vector>
#include <stddef.h>
#include <unistd.h>

constexpr size_t N = 1UL << 30;
static const long page_size = sysconf(_SC_PAGESIZE);

static void BM_Stride(benchmark::State& state) {
    size_t stride = static_cast<size_t>(state.range(0));
    static std::vector<int> v(N, 1); // allocate once

    for (auto _ : state) {
        volatile size_t sum = 0;
        for (size_t i = 0; i < N; i += stride) {
            sum += v[i];
        }
        benchmark::DoNotOptimize(sum);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(N/stride*state.iterations());
}

// Register benchmarks with different strides
BENCHMARK(BM_Stride)->Arg(1);                           // sequential, cache-friendly
BENCHMARK(BM_Stride)->Arg(page_size/sizeof(int));       // Page (4K) stride, TLB stress
BENCHMARK(BM_Stride)->Arg(2*page_size/sizeof(int));     // Two pages (8K) stride, even worse
BENCHMARK(BM_Stride)->Arg(32);                          // Smaller stride, intermediate
BENCHMARK(BM_Stride)->Arg(33);                          // Smaller stride, intermediate
BENCHMARK(BM_Stride)->Arg(512/sizeof(int));             // Smaller stride, intermediate

BENCHMARK_MAIN();
