#include <stdlib.h>
#include <string.h>
#include <vector>
#include <mutex>

#include "benchmark/benchmark.h"

template <typename L> class optional_lock_guard {
    L* lock_;
    public:
    explicit optional_lock_guard(L* lock) : lock_(lock) {
        if (lock_) lock_->lock();
    }
    ~optional_lock_guard() {
        if (lock_) lock_->unlock();
    }
    optional_lock_guard(const optional_lock_guard&) = delete;
    optional_lock_guard& operator=(const optional_lock_guard&) = delete;
    optional_lock_guard(optional_lock_guard&& rhs) : lock_(rhs.lock_) {
        rhs.lock_ = nullptr;
    }
    optional_lock_guard& operator=(optional_lock_guard&& rhs) {
        if (lock_) lock_->unlock();
        lock_ = rhs.lock_;
        rhs.lock_ = nullptr;
        return *this;
    }
};

template<typename T>
struct compare_ptr {
    bool operator()(const T* a, const T* b) const { return *a < *b; }
};

template <typename T>
class index_tree {
    using idx_t = std::set<T*, compare_ptr<T>>;
    using idx_iter_t = typename idx_t::const_iterator;
    public:
    index_tree(size_t N, bool lock) : pm_(lock ? &m_ : nullptr) { data_.reserve(N); }
    void insert(const T& t) {
        optional_lock_guard guard(pm_);
        data_.push_back(t);
        idx_.insert(&data_.back());
    }
    class const_iterator {
        idx_iter_t it_;
        public:
        const_iterator(idx_iter_t it) : it_(it) {}
        const_iterator& operator++() { ++it_; return *this; }
        const T& operator*() const { return *(*it_); }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.it_ != b.it_; }
    };
    const_iterator cbegin() const {
        optional_lock_guard guard(pm_);
        return idx_.cbegin();
    }
    const_iterator cend() const {
        optional_lock_guard guard(pm_);
        return idx_.cend();
    }
    template <typename F> bool find(F f) const {
        optional_lock_guard guard(pm_);
        for (const T& x : data_) {
            if (f(x)) return true;
        }
        return false;
    }
    private:
    std::set<T*, compare_ptr<T>> idx_;
    std::vector<T> data_;
    mutable std::mutex m_;
    std::mutex* const pm_;
};

#include <iostream>
using namespace std;

void BM_find(benchmark::State& state) {
    const unsigned int N = state.range(0);
    index_tree<unsigned long> t(N, false);
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
    index_tree<unsigned long> t(N, true);
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

