// Copyright (c) 2026 Fedor G. Pikus, fpikus@gmail.com
//  https://github.com/fpikus/LockFree
//
// MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
#include "concurrent_queue.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <timeapi.h>
  #pragma comment(lib, "winmm.lib")
  // Raise the system timer resolution to ~1 ms for the lifetime of the
  // process so Sleep() in the spin-lock fallback path is not stuck at the
  // default ~15.6 ms scheduler quantum.
  namespace {
    struct TimerResolutionGuard {
        TimerResolutionGuard()  { ::timeBeginPeriod(1); }
        ~TimerResolutionGuard() { ::timeEndPeriod(1);   }
    };
    static TimerResolutionGuard g_timer_resolution_guard;
  }
#else
  #include <unistd.h>
#endif

#include <benchmark/benchmark.h>

// Simple CHECK macros for standalone benchmark.
#ifndef CHECK
#define CHECK(x) if (!(x)) std::abort();
#endif
#ifndef CHECK_EQ
#define CHECK_EQ(a, b) if ((a) != (b)) std::abort();
#endif

template <typename Q> void test(Q* q) {
    int i = 1, j = 2;
    int* p;
    CHECK(q->push(&i));
    CHECK(q->push(&j));
    p = q->pop();
    CHECK(p);
    CHECK_EQ(*p, i);
    p = q->pop();
    CHECK(p);
    CHECK_EQ(*p, j);
}

// MPMC throughput benchmark. Google Benchmark runs this function
// concurrently on every thread of the ThreadRange; odd thread indices
// produce, even ones consume. BALANCE: after counting a miss, additionally
// perform (and count) the opposite operation, so the queue cannot settle
// permanently full (producers outnumber effective consumers) or empty.
template <typename K, typename V, size_t NTRY, size_t ALIGN, bool BALANCE = false>
void BM_MP_MC(benchmark::State& state) {
    using Q = RingAtomicMapQueueMPMC<K, V, NTRY, ALIGN>;
    // Function-local statics share the single queue across all benchmark
    // threads. Thread 0 creates it before entering the state loop (whose
    // first iteration is a start barrier for all threads) and destroys it
    // after the loop (which ends with a stop barrier), so no other thread
    // can observe a half-built or deleted queue.
    static Q* q {};
    static void* memory {};
    constexpr size_t elem_size = Q::element_size();
    const size_t N = std::bit_floor(static_cast<size_t>(state.range(0)));
    const size_t mask = N - 1;
    const size_t NB = N*elem_size;
    if (state.thread_index() == 0) {
        memory = ::operator new(NB, std::align_val_t{Q::element_align()});
        q = new Q(memory, NB);
        CHECK_EQ(q->capacity(), N);
        if (0) test(q);         // Smoke test, kept compiling; flip to 1 when debugging
    }
    // Per-thread backing array: the queue stores int*, so each producer
    // pushes pointers into its own vector -- valid, distinct, never
    // dereferenced by consumers.
    std::vector<int> v(N);
    for (size_t i = 0; i != N; ++i) v[i] = i;
    const bool producer = state.thread_index() & 1;     // Odd threads produce, even consume

    size_t p = 0, c = 0, pmiss = 0, cmiss = 0;
    for (auto _ : state) {
        if (producer) {
            if (q->push(&v[p & mask])) ++p;
            else {
                ++pmiss;
                if constexpr (BALANCE) {
                    // Queue full: do one pop instead, and count it, so the
                    // balancing work shows up in the pop rate and items/s.
                    int* ptr = q->pop();
                    if (ptr) ++c; else ++cmiss;
                }
            }
        } else {
            int* ptr = q->pop();
            if (ptr) ++c;
            else {
                ++cmiss;
                if constexpr (BALANCE) {
                    // Queue empty: do one push instead (counted, as above).
                    if (q->push(&v[0])) ++p; else ++pmiss;
                }
            }
            benchmark::DoNotOptimize(ptr);
        }
    }
    state.SetItemsProcessed(p + c);
    state.counters["push"] = benchmark::Counter(static_cast<double>(p), benchmark::Counter::kIsRate);
    state.counters["push_fail"] = benchmark::Counter(static_cast<double>(pmiss), benchmark::Counter::kIsRate);
    state.counters["pop"] = benchmark::Counter(static_cast<double>(c), benchmark::Counter::kIsRate);
    state.counters["pop_fail"] = benchmark::Counter(static_cast<double>(cmiss), benchmark::Counter::kIsRate);
    if (state.thread_index() == 0) {
        // Whatever is left in the queue is how far producers outran
        // consumers; report it as a percentage of capacity.
        size_t rem = 0;
        while (q->pop()) ++rem;
        state.counters["rem%"] = benchmark::Counter(100.0 * rem / N);
        delete q; q = nullptr;
        ::operator delete(memory, std::align_val_t{Q::element_align()});
        memory = nullptr;
    }
}

void BM_MP_MC_void_8(benchmark::State& state) { BM_MP_MC<int*, void, 8, 0>(state); }
void BM_MP_MC_void_8_balanced(benchmark::State& state) { BM_MP_MC<int*, void, 8, 0, true>(state); }
void BM_MP_MC_void_8_a8(benchmark::State& state) { BM_MP_MC<int*, void, 8, 8>(state); }
void BM_MP_MC_void_8_a16(benchmark::State& state) { BM_MP_MC<int*, void, 8, 16>(state); }
void BM_MP_MC_void_8_a32(benchmark::State& state) { BM_MP_MC<int*, void, 8, 32>(state); }
void BM_MP_MC_void_8_a64(benchmark::State& state) { BM_MP_MC<int*, void, 8, 64>(state); }
void BM_MP_MC_void_8_a128(benchmark::State& state) { BM_MP_MC<int*, void, 8, 128>(state); }
void BM_MP_MC_void_8_a256(benchmark::State& state) { BM_MP_MC<int*, void, 8, 256>(state); }

static size_t get_thread_count() {
#ifdef _WIN32
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    return static_cast<size_t>(si.dwNumberOfProcessors);
#else
    return static_cast<size_t>(sysconf(_SC_NPROCESSORS_CONF));
#endif
}
static const size_t thread_count = get_thread_count();

#define ARGS \
  ->UseRealTime() \
  ->ThreadRange(2, thread_count) \
  ->RangeMultiplier(2)->Range(1UL << 16, 1UL << 16)  

BENCHMARK(BM_MP_MC_void_8) ARGS;          // Best on M3 Pro, competitive on Grace/AMD
//BENCHMARK(BM_MP_MC_void_8_balanced) ARGS;
//BENCHMARK(BM_MP_MC_void_8_a8) ARGS;
BENCHMARK(BM_MP_MC_void_8_a16) ARGS;      // Best on M3 Pro (tied), good all-rounder
//BENCHMARK(BM_MP_MC_void_8_a32) ARGS;
BENCHMARK(BM_MP_MC_void_8_a64) ARGS;      // Best on Grace 2t, Zen3 mid-high, Cascade Lake 32t
BENCHMARK(BM_MP_MC_void_8_a128) ARGS;     // Best on Granite Rapids, Zen5 high threads
//BENCHMARK(BM_MP_MC_void_8_a256) ARGS;

BENCHMARK_MAIN();
