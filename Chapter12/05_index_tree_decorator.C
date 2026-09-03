#include <stdlib.h>
#include <string.h>
#include <vector>
#include <mutex>

#include "benchmark/benchmark.h"

template<typename T>
struct compare_ptr {
    bool operator()(const T* a, const T* b) const { return *a < *b; }
};

template <typename T>
class index_tree {
    using idx_t = std::set<T*, compare_ptr<T>>;
    using idx_iter_t = typename idx_t::const_iterator;
    public:
    index_tree(size_t N) { data_.reserve(N); }
    void insert(const T& t) {
        data_.push_back(t);
        idx_.insert(&data_.back());
    }
    template <typename F> bool find(F f) const {
        for (const T& x : data_) {
            if (f(x)) return true;
        }
        return false;
    }
    private:
    std::set<T*, compare_ptr<T>> idx_;
    std::vector<T> data_;
};

template <typename T>
class index_tree_ts : private index_tree<T> {
    public:
    using index_tree<T>::index_tree;
    void insert(const T& t) {
        std::lock_guard guard(m_);
        index_tree<T>::insert(t);
    }
    template <typename F> bool find(F&& f) const {
        std::lock_guard guard(m_);
        return index_tree<T>::find(std::forward<F>(f));
    }

    private:
    mutable std::mutex m_;
};

#include <iostream>
using namespace std;

void BM_find(benchmark::State& state) {
    const unsigned int N = state.range(0);
    index_tree<unsigned long> t(N);
    for (size_t i = 0; i < N; ++i) {
        t.insert(rand());
    }
    for (auto _ : state) {
        bool found = t.find([](unsigned long x) { return x == 0; });
        benchmark::DoNotOptimize(found);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

void BM_find_locking(benchmark::State& state) {
    const unsigned int N = state.range(0);
    index_tree_ts<unsigned long> t(N);
    for (size_t i = 0; i < N; ++i) {
        t.insert(rand());
    }
    for (auto _ : state) {
        bool found = t.find([](unsigned long x) { return x == 0; });
        benchmark::DoNotOptimize(found);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(N*state.iterations());
}

#define ARGS \
    ->Arg(1<<12)

BENCHMARK(BM_find) ARGS;
BENCHMARK(BM_find_locking) ARGS;

BENCHMARK_MAIN();

