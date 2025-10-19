#include <string.h>
#include <stdlib.h>
#include <vector>
#include <iostream>

#include "benchmark/benchmark.h"

#define REPEAT2(x) x x
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT(x) REPEAT32(x)

void SetInfo(benchmark::State& state, size_t N, size_t M) {
    char buf[1024];
    char* s = buf;
    auto bufsize = [&]() { return buf + sizeof(buf) - s; };

    double stride = M*sizeof(float);
    if (stride > 1.1*1024*1024) s += snprintf(s, bufsize(), "Stride: %g M ",  stride/1024./1024.);
    else if (stride > 1024) s += snprintf(s, bufsize(), "Stride: %g K ",  stride/1024.);
    else s += snprintf(s, bufsize(), "Stride: %g B ",  stride);

    const double mem = N*sizeof(float)*M;
    if (mem > 8*1024*1024) s += snprintf(s, bufsize(), "Memory: %g M",  mem/1024./1024.);
    else if (mem > 1024) s += snprintf(s, bufsize(), "Memory: %g K",  mem/1024.);
    else s += snprintf(s, bufsize(), "Memory: %g B",  mem);

    state.SetLabel(buf);
    state.SetItemsProcessed(N*M*state.iterations());
}

constexpr float source = 0.123456789;
constexpr size_t N = 1UL << 8;
constexpr size_t stride = 1UL << 12; // 4KB stride for 32KB 8-way cache
constexpr size_t M = stride/sizeof(float); // The worst stride possible

void BM_mem_write_seq(benchmark::State& state) {
    volatile float v[N][M]; (void)v;
    for (auto _ : state) {
        for (size_t i = 0; i < N; ++i ) {
            for (size_t j = 0; j < M; ++j ) {
                v[i][j] = source;
            }
        }
        benchmark::ClobberMemory();
    }
    SetInfo(state, N, M);
}

void BM_mem_write_conflict(benchmark::State& state) {
    volatile float v[N][M]; (void)v;
    for (auto _ : state) {
        for (size_t j = 0; j < M; ++j ) {
            for (size_t i = 0; i < N; ++i ) {
                v[i][j] = source;
            }
        }
        benchmark::ClobberMemory();
    }
    SetInfo(state, N, M);
}

void BM_mem_write_no_conflict(benchmark::State& state) {
    volatile float v[N][M+16]; (void)v;
    for (auto _ : state) {
        for (size_t j = 0; j < M; ++j ) {
            for (size_t i = 0; i < N; ++i ) {
                v[i][j] = source;
            }
        }
        benchmark::ClobberMemory();
    }
    SetInfo(state, N, M);
}

BENCHMARK(BM_mem_write_seq);
BENCHMARK(BM_mem_write_conflict);
BENCHMARK(BM_mem_write_no_conflict);

BENCHMARK_MAIN();
