#include <cstddef>
#include <cstdlib>
#include <string.h>
#include <vector>
#include <random>

#include "benchmark/benchmark.h"

#define REPEAT2(x) x x
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT(x) REPEAT32(x)


constexpr size_t data_size = 3;
struct data_t {
    unsigned long a[data_size] {};
};

struct node_t {
    node_t* next {};
    data_t d;
};

void BM_read_rand_chase(benchmark::State& state) {
    const size_t size = state.range(0);
    const size_t N = size/sizeof(node_t);
    if ((N % 32) != 0) abort();
    std::vector<node_t> data(N);

    // Build random permutation chain using actual pointers.
    std::vector<unsigned long> v_index(N);
    for (size_t i = 0; i < N; ++i) v_index[i] = i;
    std::mt19937 rg;
    std::shuffle(v_index.begin(), v_index.end(), rg);
    for (size_t i = 0; i < N; ++i) data[v_index[i]].next = &data[v_index[(i + 1) % N]];

    for (auto _ : state) {
        node_t* cur = &data[0];
        for (size_t i = 0; i != N; i += 32) {
            REPEAT(benchmark::DoNotOptimize(cur->d); cur = cur->next;)
        }
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(N*sizeof(node_t)*state.iterations());
    state.SetItemsProcessed(N*state.iterations());
    char buf[1024];
    snprintf(buf, sizeof(buf), "%lu", size);
    state.SetLabel(buf);
}

void BM_read_rand_index(benchmark::State& state) {
    const size_t size = state.range(0);
    const size_t N = size/sizeof(node_t);
    if ((N % 32) != 0) abort();
    std::vector<data_t> data(N);
    data_t* const p0 = data.data();

    std::vector<unsigned long> v_index(N);
    for (size_t i = 0; i < N; ++i) v_index[i] = i;
    std::mt19937 rg;
    std::shuffle(v_index.begin(), v_index.end(), rg);
    unsigned long* idx = v_index.data();

    for (auto _ : state) {
        for (size_t i = 0; i != N;) {
            REPEAT(benchmark::DoNotOptimize(p0[idx[i++]]);)
        }
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(N*sizeof(node_t)*state.iterations());
    state.SetItemsProcessed(N*state.iterations());
    char buf[1024];
    snprintf(buf, sizeof(buf), "%lu", size);
    state.SetLabel(buf);
}

void BM_read_rand_ptr(benchmark::State& state) {
    const size_t size = state.range(0);
    const size_t N = size/sizeof(node_t);
    if ((N % 32) != 0) abort();
    std::vector<data_t> data(N);
    std::vector<data_t*> ptrs(N);

    for (size_t i = 0; i < N; ++i) ptrs[i] = &data[i];
    std::mt19937 rg;
    std::shuffle(ptrs.begin(), ptrs.end(), rg);

    for (auto _ : state) {
        for (size_t i = 0; i != N; ) {
            REPEAT(benchmark::DoNotOptimize(*ptrs[i++]);)
        }
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(N*sizeof(node_t)*state.iterations());
    state.SetItemsProcessed(N*state.iterations());
    char buf[1024];
    snprintf(buf, sizeof(buf), "%lu", size);
    state.SetLabel(buf);
}

#define ARGS \
    ->RangeMultiplier(2)->Range(1<<10, 1<<30)

BENCHMARK(BM_read_rand_chase) ARGS;
BENCHMARK(BM_read_rand_index) ARGS;
BENCHMARK(BM_read_rand_ptr) ARGS;

BENCHMARK_MAIN();
