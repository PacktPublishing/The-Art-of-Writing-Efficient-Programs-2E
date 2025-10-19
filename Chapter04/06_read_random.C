#include <cstddef>
#include <cstdlib>
#include <string.h>
#include <vector>
#include <random>

#ifdef __x86_64__
#include <emmintrin.h>
#include <immintrin.h>
#define uint128 __m128i
#define uint256 __m256i
#endif // __x86_64__

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
void BM_read_rand(benchmark::State& state) {
    void* memory;
    const size_t size = state.range(0);
    if (size/sizeof(Word) < 32) abort();
    if (::posix_memalign(&memory, 512, size) != 0) abort();
    ::memset(memory, 0xfe, size);
    void* const end = static_cast<char*>(memory) + size;
    volatile Word* const p0 = static_cast<Word*>(memory);
    Word* const p1 = static_cast<Word*>(end);

    const size_t N = size/sizeof(Word);
    std::vector<int> v_index(N);
    for (size_t i = 0; i < N; ++i) v_index[i] = i;
    std::mt19937 rg;
    std::shuffle(v_index.begin(), v_index.end(), rg);
    int* const index = v_index.data();
    int* const i1 = index + N;

    for (auto _ : state) {
        Word sink {};
        for (const int* ind = index; ind < i1; ) {
            REPEAT(sink += *(p0 + *ind++);)
        }
        benchmark::DoNotOptimize(sink);
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

BENCHMARK_TEMPLATE1(BM_read_rand, unsigned char) ARGS;
BENCHMARK_TEMPLATE1(BM_read_rand, unsigned short) ARGS;
BENCHMARK_TEMPLATE1(BM_read_rand, unsigned int) ARGS;
BENCHMARK_TEMPLATE1(BM_read_rand, unsigned long) ARGS;
#ifdef uint128
BENCHMARK_TEMPLATE1(BM_read_rand, uint128) ARGS;
#endif
#ifdef uint256
BENCHMARK_TEMPLATE1(BM_read_rand, uint256) ARGS;
#endif

BENCHMARK_MAIN();
