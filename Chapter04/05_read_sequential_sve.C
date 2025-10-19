// 03_read_sequential.C with SVE support - for Neoverse V2 SVE2 and NEON have the same bandwidth.
#include <cstddef>
#include <cstdlib>
#include <string.h>
#include <type_traits>

template <typename T> struct is_sve_vector : std::false_type {};
template <typename T> inline constexpr bool is_sve_vector_v = is_sve_vector<T>::value;
template <typename T> struct word_traits {
    static size_t size() { return sizeof(T); }
};

#ifdef __x86_64__
#include <emmintrin.h>
#include <immintrin.h>
#define uint128 __m128i
#define uintsve __m256i
#endif // x86_64

#ifdef __ARM_NEON
#include <arm_neon.h>
#define uint128 uint64x2_t
#endif // __ARM_NEON
#ifdef __ARM_FEATURE_SVE
#include <arm_sve.h>
#define uintsve svuint64_t
template <> struct is_sve_vector<svuint64_t> : std::true_type {};
template <> struct word_traits<svuint64_t> {
    static size_t size() { return 8*svcntd(); }
};
#endif // __ARM_FEATURE_SVE

#include "benchmark/benchmark.h"

#define REPEAT2(x) x x
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT(x) REPEAT32(x)

template <class Word>
void BM_read_seq(benchmark::State& state) {
    void* memory;
    const size_t size = state.range(0);
    const size_t word_size = word_traits<Word>::size();
    if (size/word_size < 32) abort();
    if (::posix_memalign(&memory, 512, size) != 0) abort();
    ::memset(memory, 0xfe, size);
    void* const end = static_cast<char*>(memory) + size;
    volatile Word* const p0 = static_cast<Word*>(memory);
    Word* const p1 = static_cast<Word*>(end);

    for (auto _ : state) {
        for (volatile Word* p = p0; p != p1; ) {
            REPEAT(
            benchmark::DoNotOptimize(*p);
            p = (volatile Word*)((volatile char*)((volatile void*)p) + word_size);
            )
        }
        benchmark::ClobberMemory();
    }

    ::free(memory);
    state.SetBytesProcessed(size*state.iterations());
    state.SetItemsProcessed(size/word_size*state.iterations());
    char buf[1024];
    snprintf(buf, sizeof(buf), "%lu", size);
    state.SetLabel(buf);
}


#define ARGS \
    ->RangeMultiplier(2)->Range(1<<10, 1<<30)

//BENCHMARK_TEMPLATE1(BM_read_seq, unsigned char) ARGS;
//BENCHMARK_TEMPLATE1(BM_read_seq, unsigned short) ARGS;
//BENCHMARK_TEMPLATE1(BM_read_seq, unsigned int) ARGS;
BENCHMARK_TEMPLATE1(BM_read_seq, unsigned long) ARGS;
#ifdef uint128
BENCHMARK_TEMPLATE1(BM_read_seq, uint128) ARGS;
#endif // uint128
#ifdef uintsve
BENCHMARK_TEMPLATE1(BM_read_seq, uintsve) ARGS;
#endif // uintsve

BENCHMARK_MAIN();
