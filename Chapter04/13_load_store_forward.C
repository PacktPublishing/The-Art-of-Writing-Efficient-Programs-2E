// Demonstration of store-to-load forwarding on AMD chips:
// when executing "read-add-write-read-..." sequence, store-to-load forwarding
// sends the result of the write to next operation without waiting for write to
// complete. Zen 3 can track dependency chain of 24 adds. For 25th add, it
// waits for the write to complete in L1 and read the value back, causing a
// pipeline stall.
#include <cstdlib>

#include "benchmark/benchmark.h"

volatile long a = 0;

#define REPEAT2(x) {x} {x}
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT64(x) REPEAT32(x) REPEAT32(x)
#define REPEAT128(x) REPEAT64(x) REPEAT64(x)
#define REPEAT256(x) REPEAT128(x) REPEAT128(x)
#define REPEAT512(x) REPEAT256(x) REPEAT256(x)
#define REPEAT1K(x) REPEAT512(x) REPEAT512(x)

void BM_4(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4;
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*4*state.iterations());
}

void BM_8(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8;
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*8*state.iterations());
}

void BM_16(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10; a += 11; a += 12; a += 13; a += 14; a += 15; a += 16;
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*16*state.iterations());
}

void BM_24(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10; a += 11; a += 12; a += 13; a += 14; a += 15; a += 16;
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8;
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*24*state.iterations());
}

void BM_25(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10; a += 11; a += 12; a += 13; a += 14; a += 15; a += 16;
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9;
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*25*state.iterations());
}

void BM_26(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10; a += 11; a += 12; a += 13; a += 14; a += 15; a += 16;
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10;
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*26*state.iterations());
}

void BM_28(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10; a += 11; a += 12; a += 13; a += 14; a += 15; a += 16;
            a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10; a += 11; a += 12;
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*28*state.iterations());
}

void BM_32(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            REPEAT2(a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10; a += 11; a += 12; a += 13; a += 14; a += 15; a += 16;)
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*16*2*state.iterations());
}

void BM_64(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        for (size_t i = 0; i != N; ++i) {
            REPEAT4(a += 1; a += 2; a += 3; a += 4; a += 5; a += 6; a += 7; a += 8; a += 9; a += 10; a += 11; a += 12; a += 13; a += 14; a += 15; a += 16;)
        }
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*16*4*state.iterations());
}

#define ARGS \
    ->Arg(1<<16)

BENCHMARK(BM_4) ARGS;
BENCHMARK(BM_8) ARGS;
BENCHMARK(BM_16) ARGS;
BENCHMARK(BM_24) ARGS;
BENCHMARK(BM_25) ARGS;
BENCHMARK(BM_26) ARGS;
BENCHMARK(BM_28) ARGS;
BENCHMARK(BM_32) ARGS;
BENCHMARK(BM_64) ARGS;

BENCHMARK_MAIN();

