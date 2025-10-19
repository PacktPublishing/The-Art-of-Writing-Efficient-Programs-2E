#include <cstddef>
#include <cstdlib>
#include <string.h>

#ifdef __x86_64__
#include <emmintrin.h>
#include <immintrin.h>
#define uint128 __m128i
#define uint256 __m256i
#endif // x86_64

#ifdef __ARM_NEON
#include <arm_neon.h>
#define uint128 uint64x2_t
#endif // __ARM_NEON

#include "benchmark/benchmark.h"

#define REPEAT2(x) x x
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT(x) REPEAT32(x)

template <class Word>
void BM_write_seq(benchmark::State& state) {
    void* memory;
    const size_t size = state.range(0);
    if (size/sizeof(Word) < 32) abort();
    if (::posix_memalign(&memory, 512, size) != 0) abort();
    void* const end = static_cast<char*>(memory) + size;
    volatile Word* const p0 = static_cast<Word*>(memory);
    Word* const p1 = static_cast<Word*>(end);
    Word fill; ::memset(&fill, 0xab, sizeof(fill));

    for (auto _ : state) {
        for (volatile Word* p = p0; p != p1; ) {
            REPEAT(*p++ = fill;)
        }
        benchmark::ClobberMemory();
    }

    ::free(memory);
    state.SetBytesProcessed(size*state.iterations());
    state.SetItemsProcessed((p1 - p0)*state.iterations());
    char buf[1024];
    snprintf(buf, sizeof(buf), "%lu", size);
    state.SetLabel(buf);
}


#define ARGS \
    ->RangeMultiplier(2)->Range(1<<10, 1<<30)

BENCHMARK_TEMPLATE1(BM_write_seq, unsigned char) ARGS;
BENCHMARK_TEMPLATE1(BM_write_seq, unsigned short) ARGS;
BENCHMARK_TEMPLATE1(BM_write_seq, unsigned int) ARGS;
BENCHMARK_TEMPLATE1(BM_write_seq, unsigned long) ARGS;
#ifdef uint128
BENCHMARK_TEMPLATE1(BM_write_seq, uint128) ARGS;
#endif // uint128
#ifdef uint256
BENCHMARK_TEMPLATE1(BM_write_seq, uint256) ARGS;
#endif // uint256

BENCHMARK_MAIN();
