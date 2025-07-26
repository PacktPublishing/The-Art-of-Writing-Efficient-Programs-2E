#include <cstdlib>

#include "benchmark/benchmark.h"

volatile long a = 0;

__attribute__((always_inline))
inline void loop_body(size_t N) {
    size_t i = 0;
loop:
        a += 1; a += 2; a += 3;// a += 4;       // Make sure the loop is short enough to fit into 64 bytes
        //a += 5; a += 6; //a += 7; a += 8;
    if (++i != N) goto loop;
}

__attribute__((aligned(64))) // Start the function on a 64-byte boundary.
__attribute__((noinline))
void f_aligned(size_t N) {
    asm volatile(
        ".rept 49\n\t"  // Check assembly and figure out the count to align the loop at the 64-byte boundary
        "nop\n\t"
        ".endr"
    );
    loop_body(N);
}

__attribute__((aligned(64))) // Start the function on a 64-byte boundary.
__attribute__((noinline))
void f_misaligned(size_t N) {
    asm volatile(
        ".rept 10\n\t"  // Check assembly and figure out the count to make sure the loop crosses the 64-byte boundary
        "nop\n\t"
        ".endr"
    );
    loop_body(N);
}

void BM_aligned(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        f_aligned(N);
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*3*state.iterations());
}

void BM_misaligned(benchmark::State& state) {
    const unsigned int N = state.range(0);
    for (auto _ : state) {
        f_misaligned(N);
    }
    benchmark::DoNotOptimize(a);
    state.SetItemsProcessed(N*3*state.iterations());
}

#define ARGS \
    ->Arg(1<<22)

BENCHMARK(BM_misaligned) ARGS;
BENCHMARK(BM_aligned) ARGS;

BENCHMARK_MAIN();

