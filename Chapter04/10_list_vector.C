#include <stdlib.h>
#include <string.h>
#include <vector>
#include <list>
#include <memory>

#include "benchmark/benchmark.h"

using T = double;

static size_t mem = 0;
template <typename T> struct alloc : public std::allocator<T> {
    using base_t = std::allocator<T>;
    using base_t::allocator;
    using pointer = T*;
    using size_type = size_t;
    template <typename U> struct rebind { typedef alloc<U> other; };
    pointer allocate(size_type n) {
        mem += n*sizeof(T);
        return base_t::allocate(n);
    }
    void deallocate(T* p, std::size_t n) {
        mem -= n*sizeof(T);
        base_t::deallocate(p, n);
    }
};

void BM_vector(benchmark::State& state) {
    srand(1);
    const size_t N = state.range(0);
    std::vector<T, alloc<T>> c(N);
    for (auto _ : state) {
        T i{};
        for (T& x : c) {
            x = ++i;
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
    state.SetBytesProcessed(N*sizeof(T)*state.iterations());
    char buf[1024];
    if (mem > 8*1024*1024) snprintf(buf, sizeof(buf), "Memory: %g M",  mem/1024./1024.);
    else if (mem > 1024) snprintf(buf, sizeof(buf), "Memory: %g K",  mem/1024.);
    else snprintf(buf, sizeof(buf), "Memory: %g B",  mem/1.);
    state.SetLabel(buf);
}

void BM_list_ctor(benchmark::State& state) {
    srand(1);
    const size_t N = state.range(0);
    std::list<T, alloc<T>> c(N);
    for (auto _ : state) {
        T i{};
        for (T& x : c) {
            x = ++i;
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
    state.SetBytesProcessed(N*sizeof(T)*state.iterations());
    char buf[1024];
    if (mem > 8*1024*1024) snprintf(buf, sizeof(buf), "Memory: %g M",  mem/1024./1024.);
    else if (mem > 1024) snprintf(buf, sizeof(buf), "Memory: %g K",  mem/1024.);
    else snprintf(buf, sizeof(buf), "Memory: %g B",  mem/1.);
    state.SetLabel(buf);
}

void BM_list(benchmark::State& state) {
    srand(1);
    const size_t N = state.range(0);
    std::list<T, alloc<T>> c(N);
    for (size_t i = 0; i != 4; ++i) {
        bool erase = false;
        for (auto it = c.begin(); it != c.end();) {
            if (erase) {
                auto it0 = it++;
                c.erase(it0);
            } else {
                ++it;
            }
            erase = !erase;
        }
        c.resize(N);
    }
    for (auto _ : state) {
        T i{};
        for (T& x : c) {
            x = ++i;
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
    state.SetBytesProcessed(N*sizeof(T)*state.iterations());
    char buf[1024];
    if (mem > 8*1024*1024) snprintf(buf, sizeof(buf), "Memory: %g M",  mem/1024./1024.);
    else if (mem > 1024) snprintf(buf, sizeof(buf), "Memory: %g K",  mem/1024.);
    else snprintf(buf, sizeof(buf), "Memory: %g B",  mem/1.);
    state.SetLabel(buf);
}

//#define ARG RangeMultiplier(4)->Range(1<<2, 1<<26)
#define ARG Arg(1UL << 22)
BENCHMARK(BM_vector)->ARG;
//BENCHMARK(BM_list_ctor)->ARG;
BENCHMARK(BM_list)->ARG;

BENCHMARK_MAIN();

