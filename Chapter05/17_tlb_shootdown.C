#include <benchmark/benchmark.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <asm/unistd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>

#include <thread>
#include <atomic>
#include <vector>
#include <cstring>
#include <cassert>
#include <iostream>


// ---- globals ----
static const long page_size = sysconf(_SC_PAGESIZE);
static constexpr size_t N = 1UL << 28; // 256 MB


// ---- Control benchmark ----
static void BM_TLB_Control(benchmark::State& state) {
    static char* mem = nullptr;

    if (state.thread_index() == 0) {
        mem = static_cast<char*>(mmap(nullptr, N, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        assert(mem != MAP_FAILED);
        for (size_t i = 0; i < N; i += page_size) mem[i] = 1;
    }

    for (auto _ : state) {
        if (state.thread_index() == 0) {
            volatile size_t sum = 0;
            for (size_t i = 0; i < N; i += page_size) sum += mem[i];
            benchmark::DoNotOptimize(sum);
        } else {
        }
    }

    if (state.thread_index() == 0) {
        munmap(mem, N);
        state.SetItemsProcessed(N/page_size*state.iterations());
    }
}


// ---- Shootdown benchmark ----
static void BM_TLB_Shootdown(benchmark::State& state) {
    static char* mem = nullptr;

    if (state.thread_index() == 0) {
        mem = static_cast<char*>(mmap(nullptr, N, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        assert(mem != MAP_FAILED);
        for (size_t i = 0; i < N; i += page_size) mem[i] = 1;
    }

    for (auto _ : state) {
        if (state.thread_index() == 0) {
            volatile size_t sum = 0;
            for (size_t i = 0; i < N; i += page_size) sum += mem[i];
            benchmark::DoNotOptimize(sum);
        } else {
            mprotect(mem, N, PROT_READ);
            (void)*(volatile char*)mem;
            mprotect(mem, N, PROT_READ | PROT_WRITE);
        }
    }

    if (state.thread_index() == 0) {
        munmap(mem, N);
        state.SetItemsProcessed(N/page_size*state.iterations());
    }
}

BENCHMARK(BM_TLB_Control)->UseRealTime()->Threads(2);
BENCHMARK(BM_TLB_Shootdown)->UseRealTime()->Threads(2);

BENCHMARK_MAIN();
