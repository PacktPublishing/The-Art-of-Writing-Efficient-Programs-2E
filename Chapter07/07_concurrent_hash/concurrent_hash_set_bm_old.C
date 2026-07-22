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
#include <benchmark/benchmark.h>
#include <unistd.h>
#include "concurrent_hash_set.h"
#include <random>
/*
 * ConcurrentResizableHashSet Benchmarks
 * 
 * This suite measures the multi-threaded scalability and wait-free characteristics of 
 * the ConcurrentResizableHashSet under various simulated workloads.
 * 
 * Benchmark Configurations:
 *  - Collision Scenarios: All tests are templated to run against `std::hash<int>` 
 *    (simulating ideal, uniform load distribution) and `BadHash` (a custom hash that
 *    forces elements into exactly 16 buckets to simulate worst-case heavy contention).
 *  - Insert_MostlyNew: Pushes randomly generated integers from a massive pool (10,000,000). 
 *    High rate of successful insertions, constantly forcing internal DCLP resizes and 
 *    chain splitting.
 *  - Insert_MostlyExisting: Pushes integers from a tiny pool (1-10). High rate of duplicate 
 *    keys acting primarily as wait-free tree strides with zero allocations.
 *  - 1W_NR (1 Writer, N Readers): Thread 0 acts as a continuous inserter while all other 
 *    threads strictly poll lock-free queries (`contains()`) in parallel.
 *  - Mixed Workloads (10W90R / 50W50R / 90W10R): Parameterized read/write execution 
 *    patterns that mirror typical runtime environments.
 */

static const int num_cpu = sysconf(_SC_NPROCESSORS_CONF);

struct BadHash {
    size_t operator()(int x) const { return x % 16; } // Force heavy collisions
};

template <typename Hash>
class HashSetFixture : public benchmark::Fixture {
public:
    static ConcurrentResizableHashSet<int, false, Hash>* set;

    void SetUp(const ::benchmark::State& state) override {
        if (state.thread_index() == 0) {
            set = new ConcurrentResizableHashSet<int, false, Hash>(1024);
        }
    }

    void TearDown(const ::benchmark::State& state) override {
        if (state.thread_index() == 0) {
            delete set;
            set = nullptr;
        }
    }
};

template <typename Hash>
ConcurrentResizableHashSet<int, false, Hash>* HashSetFixture<Hash>::set = nullptr;

#define SETUP_RNG \
    std::mt19937 gen(state.thread_index() + 42 + state.iterations()); \
    std::uniform_int_distribution<int> dist(0, 10000000); \
    std::uniform_int_distribution<int> small_dist(0, 10); \
    std::uniform_int_distribution<int> pct_dist(0, 99)

// 1. Insert Mostly New
BENCHMARK_TEMPLATE_DEFINE_F(HashSetFixture, Insert_MostlyNew_StdHash, std::hash<int>)(benchmark::State& state) {
    SETUP_RNG;
    for (auto _ : state) {
        benchmark::DoNotOptimize(set->insert(dist(gen)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(HashSetFixture, Insert_MostlyNew_StdHash)->ThreadRange(1, num_cpu);

BENCHMARK_TEMPLATE_DEFINE_F(HashSetFixture, Insert_MostlyNew_BadHash, BadHash)(benchmark::State& state) {
    SETUP_RNG;
    for (auto _ : state) {
        benchmark::DoNotOptimize(set->insert(dist(gen)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(HashSetFixture, Insert_MostlyNew_BadHash)->ThreadRange(1, num_cpu);

// 2. Insert Mostly Existing
BENCHMARK_TEMPLATE_DEFINE_F(HashSetFixture, Insert_MostlyExisting_StdHash, std::hash<int>)(benchmark::State& state) {
    SETUP_RNG;
    for (auto _ : state) {
        benchmark::DoNotOptimize(set->insert(small_dist(gen)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(HashSetFixture, Insert_MostlyExisting_StdHash)->ThreadRange(1, num_cpu);

BENCHMARK_TEMPLATE_DEFINE_F(HashSetFixture, Insert_MostlyExisting_BadHash, BadHash)(benchmark::State& state) {
    SETUP_RNG;
    for (auto _ : state) {
        benchmark::DoNotOptimize(set->insert(small_dist(gen)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(HashSetFixture, Insert_MostlyExisting_BadHash)->ThreadRange(1, num_cpu);

// 3. 1 Writer, N Readers
BENCHMARK_TEMPLATE_DEFINE_F(HashSetFixture, 1W_NR_StdHash, std::hash<int>)(benchmark::State& state) {
    SETUP_RNG;
    for (auto _ : state) {
        if (state.thread_index() == 0) {
            set->insert(dist(gen));
        } else {
            set->contains(dist(gen));
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(HashSetFixture, 1W_NR_StdHash)->ThreadRange(2, num_cpu);

BENCHMARK_TEMPLATE_DEFINE_F(HashSetFixture, 1W_NR_BadHash, BadHash)(benchmark::State& state) {
    SETUP_RNG;
    for (auto _ : state) {
        if (state.thread_index() == 0) {
            set->insert(dist(gen));
        } else {
            set->contains(dist(gen));
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK_REGISTER_F(HashSetFixture, 1W_NR_BadHash)->ThreadRange(2, num_cpu);

// 4. Mixed Workloads
/*
 * CAUTION: Google Benchmark Setup Synchronization Subtlety
 * 
 * In earlier tests, defining `RunMixed` as a standalone helper function and passing 
 * `HashSetFixture::set` as an argument caused random Segmentation Faults.
 * 
 * Why? Google Benchmark's `SetUp()` is executed per-thread, but there is NO implicit 
 * thread barrier *between* `SetUp()` and the start of the benchmark function body. 
 * Thread 1 could enter the benchmark function and evaluate the `set` argument (which 
 * was still `nullptr`) before Thread 0 finished allocating it in its `SetUp()`.
 * 
 * The ONLY implicit barrier provided by Google Benchmark is the `for (auto _ : state)` loop. 
 * By defining this logic as a macro that evaluates `set->insert` INSIDE the loop, we 
 * guarantee that ALL threads have completely finished `SetUp()` before dereferencing `set`.
 */
#define DEFINE_MIXED_BM(NAME, HASH_TYPE, WRITE_PCT) \
    BENCHMARK_TEMPLATE_DEFINE_F(HashSetFixture, NAME, HASH_TYPE)(benchmark::State& state) { \
        SETUP_RNG; \
        for (auto _ : state) { \
            if (pct_dist(gen) < WRITE_PCT) { \
                benchmark::DoNotOptimize(set->insert(dist(gen))); \
            } else { \
                benchmark::DoNotOptimize(set->contains(dist(gen))); \
            } \
        } \
        state.SetItemsProcessed(state.iterations()); \
    } \
    BENCHMARK_REGISTER_F(HashSetFixture, NAME)->ThreadRange(1, num_cpu);

DEFINE_MIXED_BM(Mixed_10W90R_StdHash, std::hash<int>, 10)
DEFINE_MIXED_BM(Mixed_10W90R_BadHash, BadHash, 10)

DEFINE_MIXED_BM(Mixed_50W50R_StdHash, std::hash<int>, 50)
DEFINE_MIXED_BM(Mixed_50W50R_BadHash, BadHash, 50)

DEFINE_MIXED_BM(Mixed_90W10R_StdHash, std::hash<int>, 90)
DEFINE_MIXED_BM(Mixed_90W10R_BadHash, BadHash, 90)

BENCHMARK_MAIN();
