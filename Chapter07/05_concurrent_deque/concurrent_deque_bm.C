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
#include "concurrent_deque.h"
#include "spinlock.h"
#include <deque>
#include <thread>
#include <vector>
#include <atomic>
#include <barrier>
#include <memory>
#include <unistd.h>

// Determine the maximum number of hardware threads available on this system.
// This allows Google Benchmark to automatically scale the tests up to the
// machine's physical limits (e.g., all 256 threads on a Granite Rapids server)
// without hardcoding artificial ceilings.
static const int num_cpu = sysconf(_SC_NPROCESSORS_CONF);

// ============================================================================
// SpinlockDeque Baseline
// ============================================================================
// A simple thread-safe wrapper around std::deque using a SpinLock.
// This serves as the baseline to demonstrate the catastrophic performance
// collapse (thundering herd) that occurs when using locks under high contention,
// compared to the lock-free ConcurrentAppendDeque.
template <typename T>
class SpinlockDeque {
public:
    void push_back(const T& val) {
        std::lock_guard<SpinLock> lock(spinlock_);
        deque_.push_back(val);
    }
    
    void resize(size_t new_size) {
        std::lock_guard<SpinLock> lock(spinlock_);
        if (new_size > deque_.size()) {
            deque_.resize(new_size);
        }
    }

    size_t size() const {
        std::lock_guard<SpinLock> lock(spinlock_);
        return deque_.size();
    }
    
    T& operator[](size_t index) {
        std::lock_guard<SpinLock> lock(spinlock_);
        return deque_[index];
    }
    
    const T& operator[](size_t index) const {
        std::lock_guard<SpinLock> lock(spinlock_);
        return deque_[index];
    }
    
private:
    mutable SpinLock spinlock_;
    std::deque<T> deque_;
};

// ============================================================================
// Benchmark Concurrency Design Principles
// ============================================================================
// These benchmarks do NOT use Google Benchmark's native MT (state.threads())
// nor do they create std::threads inside the timing loop. Why?
// 1. Thread Creation Overhead: Spawning threads takes significant OS time. If 
//    done inside the loop, the benchmark measures OS latency, not container speed.
// 2. Staggered Starts: Without precise synchronization, Thread 1 might finish its
//    work before Thread 8 even wakes up. This destroys concurrent contention.
// 
// Solution: We spawn a persistent pool of `std::jthread`s exactly once.
// We use `std::barrier` to perfectly align all threads at a starting line.
// The main thread pauses the clock, resets the state, and then releases the 
// barrier, ensuring every thread hits the container at the exact same microsecond.
//
// Clean Shutdown: While `std::jthread` is often used simply as an RAII wrapper 
// to avoid manual `.join()` calls, we actively use its cooperative cancellation 
// mechanism (`std::stop_token`). The main thread calls `.request_stop()` on all 
// threads and then unblocks the barrier one last time, allowing the workers to 
// check `stoken.stop_requested()` and cleanly exit their infinite loops.

// ============================================================================
// Scenario 1: Element Access with No Growth
// ============================================================================
// Goal: Measure pure, wait-free scaling when the deque is already sized.
// Expectation: Perfect linear scaling up to the memory bandwidth or ALU limit,
// as threads write to strictly disjoint ranges without any atomic synchronization.
template <typename DequeType>
static void BM_AccessNoGrowth(benchmark::State& state) {
    size_t elements_per_thread = state.range(0);
    int num_threads = state.range(1);
    size_t total_elements = num_threads * elements_per_thread;
    
    // Pre-allocate the deque so no resizes occur during the benchmark.
    DequeType dq;
    dq.resize(total_elements);
    
    // num_threads workers + 1 main coordinator thread.
    std::barrier sync_start(num_threads + 1);
    std::barrier sync_end(num_threads + 1);
    
    // Using std::jthread automatically requests a stop and joins on destruction.
    std::vector<std::jthread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t](std::stop_token stoken) {
            size_t start_idx = t * elements_per_thread;
            size_t end_idx = start_idx + elements_per_thread;
            
            while (true) {
                // Wait at the starting line for the main thread to resume timing.
                sync_start.arrive_and_wait();
                if (stoken.stop_requested()) break;
                
                // Payload: Pure wait-free writes to disjoint memory.
                for (size_t i = start_idx; i < end_idx; ++i) {
                    dq[i] = static_cast<int>(i);
                }
                
                // Wait for all threads to finish before the next iteration.
                sync_end.arrive_and_wait();
            }
        });
    }
    
    for (auto _ : state) {
        sync_start.arrive_and_wait(); // Unleash threads
        sync_end.arrive_and_wait();   // Wait for completion
    }
    
    // Cleanly shut down the thread pool
    for (auto& thread : threads) {
        thread.request_stop();
    }
    sync_start.arrive_and_wait(); // Unblock them one last time so they see the stop request
    threads.clear();
    
    // Custom counter for easy-to-read throughput (e.g., 20 G/s)
    state.counters["Elements/s"] = benchmark::Counter(
        state.iterations() * total_elements,
        benchmark::Counter::kIsRate);
}

BENCHMARK_TEMPLATE(BM_AccessNoGrowth, SpinlockDeque<int>)
    ->ArgsProduct({
        benchmark::CreateRange(1000, 100000, /*multi=*/10),
        benchmark::CreateRange(1, num_cpu, /*multi=*/2)
    })->UseRealTime();

BENCHMARK_TEMPLATE(BM_AccessNoGrowth, ConcurrentAppendDeque<int, 1024>)
    ->ArgsProduct({
        benchmark::CreateRange(1000, 100000, /*multi=*/10),
        benchmark::CreateRange(1, num_cpu, /*multi=*/2)
    })->UseRealTime();

// ============================================================================
// Scenario 2: Element Access with Size Check
// ============================================================================
// Goal: Measure the overhead of the `memory_order_acquire` penalty.
// In reality, a reader thread must check `size()` before blindly indexing. 
// This benchmark forces every thread to bounce the atomic `size_` cache line 
// once per iteration before writing to its disjoint range.
template <typename DequeType>
static void BM_AccessWithSizeCheck(benchmark::State& state) {
    size_t elements_per_thread = state.range(0);
    int num_threads = state.range(1);
    size_t total_elements = num_threads * elements_per_thread;
    
    DequeType dq;
    dq.resize(total_elements);
    
    std::barrier sync_start(num_threads + 1);
    std::barrier sync_end(num_threads + 1);
    
    std::vector<std::jthread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t](std::stop_token stoken) {
            size_t start_idx = t * elements_per_thread;
            size_t end_idx = start_idx + elements_per_thread;
            
            while (true) {
                sync_start.arrive_and_wait();
                if (stoken.stop_requested()) break;
                
                // The single `acquire` load on the atomic size.
                if (dq.size() >= end_idx) {
                    for (size_t i = start_idx; i < end_idx; ++i) {
                        dq[i] = static_cast<int>(i);
                    }
                }
                
                sync_end.arrive_and_wait();
            }
        });
    }
    
    for (auto _ : state) {
        sync_start.arrive_and_wait(); // Unleash threads
        sync_end.arrive_and_wait();   // Wait for completion
    }
    
    for (auto& thread : threads) {
        thread.request_stop();
    }
    sync_start.arrive_and_wait();
    threads.clear();
    
    state.counters["Elements/s"] = benchmark::Counter(
        state.iterations() * total_elements,
        benchmark::Counter::kIsRate);
}

BENCHMARK_TEMPLATE(BM_AccessWithSizeCheck, SpinlockDeque<int>)
    ->ArgsProduct({
        benchmark::CreateRange(1000, 100000, /*multi=*/10),
        benchmark::CreateRange(1, num_cpu, /*multi=*/2)
    })->UseRealTime();

BENCHMARK_TEMPLATE(BM_AccessWithSizeCheck, ConcurrentAppendDeque<int, 1024>)
    ->ArgsProduct({
        benchmark::CreateRange(1000, 100000, /*multi=*/10),
        benchmark::CreateRange(1, num_cpu, /*multi=*/2)
    })->UseRealTime();

// ============================================================================
// Scenario 3: Resize and Write
// ============================================================================
// Goal: Stress test the Double-Checked Locking Pattern (DCLP) and lock-free
// contention during container expansion.
// The deque starts empty. All threads simultaneously try to resize it to their
// required capacity, guaranteeing a massive "Thundering Herd" collision.
template <typename DequeType>
static void BM_ResizeAndWrite(benchmark::State& state) {
    size_t elements_per_thread = state.range(0);
    int num_threads = state.range(1);
    
    // Benchmark Balance Decision:
    // If we only write each element once, the extremely slow heap allocations
    // and spinlock contention inside `resize()` will completely dominate the time,
    // masking the wait-free read/write throughput we actually care about.
    // Real-world usage involves resizing rarely, and accessing data heavily.
    // Therefore, we amplify the access workload by 100x to simulate this reality.
    const int work_iterations = 100;
    size_t total_elements = num_threads * elements_per_thread * work_iterations;
    
    std::unique_ptr<DequeType> dq;
    
    std::barrier sync_start(num_threads + 1);
    std::barrier sync_end(num_threads + 1);
    
    std::vector<std::jthread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t](std::stop_token stoken) {
            size_t start_idx = t * elements_per_thread;
            size_t end_idx = start_idx + elements_per_thread;
            
            while (true) {
                sync_start.arrive_and_wait();
                if (stoken.stop_requested()) break;
                
                // The Thundering Herd hits the DCLP here. Only one thread
                // will successfully grab the lock and allocate the blocks.
                dq->resize(end_idx);
                
                // The amplified read/write workload. 
                // We add `iter` to prevent compiler loop-unrolling optimizations.
                for (int iter = 0; iter < work_iterations; ++iter) {
                    for (size_t i = start_idx; i < end_idx; ++i) {
                        (*dq)[i] = static_cast<int>(i + iter);
                    }
                }
                
                sync_end.arrive_and_wait();
            }
        });
    }
    
    for (auto _ : state) {
        // Pause the timer so we do not benchmark the teardown of the old
        // deque or the instantiation of the new one. We strictly only want
        // to measure the concurrent scaling.
        state.PauseTiming();
        dq = std::make_unique<DequeType>();
        state.ResumeTiming();
        
        sync_start.arrive_and_wait(); // Unleash threads
        sync_end.arrive_and_wait();   // Wait for completion
    }
    
    for (auto& thread : threads) {
        thread.request_stop();
    }
    sync_start.arrive_and_wait();
    threads.clear();
    
    state.counters["Elements/s"] = benchmark::Counter(
        state.iterations() * total_elements,
        benchmark::Counter::kIsRate);
}

BENCHMARK_TEMPLATE(BM_ResizeAndWrite, SpinlockDeque<int>)
    ->ArgsProduct({
        benchmark::CreateRange(1000, 100000, /*multi=*/10),
        benchmark::CreateRange(1, num_cpu, /*multi=*/2)
    })->UseRealTime();

BENCHMARK_TEMPLATE(BM_ResizeAndWrite, ConcurrentAppendDeque<int, 1024>)
    ->ArgsProduct({
        benchmark::CreateRange(1000, 100000, /*multi=*/10),
        benchmark::CreateRange(1, num_cpu, /*multi=*/2)
    })->UseRealTime();

BENCHMARK_MAIN();
