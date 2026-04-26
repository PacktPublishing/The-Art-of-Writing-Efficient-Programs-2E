#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <numa.h>

#include <iostream>
#include <atomic>

#include <benchmark/benchmark.h>

static const size_t numcpu = sysconf(_SC_NPROCESSORS_CONF);
static const size_t numnodes = numa_num_configured_nodes();

static const size_t* const node_of_cpu = []() {
    size_t* node_of_cpu = new size_t[numcpu];
    for (size_t i = 0; i != numcpu; ++i) {
        node_of_cpu[i] = ::numa_node_of_cpu(i);
    }
    return node_of_cpu;
}();
static const cpu_set_t* node_cpu_mask = []() {
    std::cout << "Running on " << numcpu << " CPUs (" << numnodes << " NUMA nodes)" << std::endl;
    cpu_set_t* mask = new cpu_set_t[numnodes];
    for (size_t i = 0; i != numnodes; ++i) {
        CPU_ZERO(&mask[i]);
    }
    for (size_t i = 0; i != numcpu; ++i) {
        CPU_SET(i, &mask[node_of_cpu[i]]);
    }
    return mask;
}();

#define REPEAT2(x) x x
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT(x) REPEAT32(x)

std::atomic<size_t> x;

void BM_increment_node0(benchmark::State& state) {
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &node_cpu_mask[0]) != 0) { std::cout << "Pinning failed on line " << __LINE__ << std::endl; abort(); }
    for (auto _ : state) {
        REPEAT(x.fetch_add(1, std::memory_order_relaxed);)
    }
    state.SetItemsProcessed(state.iterations()*32);
}

void BM_increment_node_pinned_threads(benchmark::State& state) {
    if (numnodes <= 1) state.SkipWithError("No NUMA nodes");
    const size_t node = state.thread_index() % numnodes;
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &node_cpu_mask[node]) != 0) { std::cout << "Pinning failed on line " << __LINE__ << std::endl; abort(); }
    for (auto _ : state) {
        REPEAT(x.fetch_add(1, std::memory_order_relaxed);)
    }
    state.SetItemsProcessed(state.iterations()*32);
}

void BM_increment_cpu_pinned_threads(benchmark::State& state) {
    const size_t cpu = state.thread_index() % numcpu;
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu, &cpu_set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) != 0) { std::cout << "Pinning failed on line " << __LINE__ << std::endl; abort(); }
    for (auto _ : state) {
        REPEAT(x.fetch_add(1, std::memory_order_relaxed);)
    }
    state.SetItemsProcessed(state.iterations()*32);
}

#define ARG(N) UseRealTime()->Threads(numcpu/N)

BENCHMARK(BM_increment_node0)->ARG(1);
BENCHMARK(BM_increment_node_pinned_threads)->ARG(1);
BENCHMARK(BM_increment_cpu_pinned_threads)->ARG(1);

BENCHMARK(BM_increment_node0)->ARG(2);
BENCHMARK(BM_increment_node_pinned_threads)->ARG(2);
BENCHMARK(BM_increment_cpu_pinned_threads)->ARG(2);

BENCHMARK(BM_increment_node0)->ARG(4);
BENCHMARK(BM_increment_node_pinned_threads)->ARG(4);
BENCHMARK(BM_increment_cpu_pinned_threads)->ARG(4);

BENCHMARK_MAIN();
