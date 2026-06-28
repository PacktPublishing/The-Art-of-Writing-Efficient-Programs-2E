#include <coroutine>
#include <deque>
#include <cstdint>
#include <generator>
#include "benchmark/benchmark.h"

#ifdef NOINLINE
#define ATTR __attribute__((noinline))
#else
#define ATTR
#endif // NOINLINE

// ============================================================
// Coroutine generator
// ============================================================
#ifdef CPP23_GENERATOR
template <typename T> using generator = std::generator<T>;
#else
template <typename T> struct generator {
  struct promise_type {
    T* ptr_;
    generator get_return_object() {
      return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never  initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() {}
    std::suspend_always yield_value(T& v) { ptr_ = &v; return {}; }
    void return_void() {}
  };

  std::coroutine_handle<promise_type> h_;
  explicit generator(std::coroutine_handle<promise_type> h) : h_(h) {}
  generator(const generator&) = delete;
  generator(generator&& o) : h_(std::exchange(o.h_, {})) {}
  ~generator() { if (h_) h_.destroy(); }

  struct iterator {
    std::coroutine_handle<promise_type> h_;
    T& operator*() const { return *h_.promise().ptr_; }
    iterator& operator++() { h_(); return *this; }
    bool operator!=(std::default_sentinel_t) const { return !h_.done(); }
  };
  iterator begin() { return {h_}; }
  std::default_sentinel_t end() { return {}; }
};
#endif // CPP23

// ============================================================
// 4D matrix stored in std::deque
// ============================================================
template <typename T> struct Matrix4D {
  static constexpr int64_t PAD = 64 / sizeof(T);
  std::deque<T> data;
  int64_t n0, n1, n2, n3;

  Matrix4D(int64_t a, int64_t b, int64_t c, int64_t d)
    : data(a*b*c*(d+PAD), T(1)), n0(a), n1(b), n2(c), n3(d) {}

  T& at(int64_t i, int64_t j, int64_t k, int64_t l) {
    return data[((i*n1+j)*n2+k)*(n3+PAD)+l];
  }
};

// ============================================================
// Approach 1: coroutine generator as iterator
// ============================================================
template <typename T>
ATTR generator<T> matrix_gen_row(Matrix4D<T>& m) {
  for (int64_t i = 0; i < m.n0; ++i)
    for (int64_t j = 0; j < m.n1; ++j)
      for (int64_t k = 0; k < m.n2; ++k)
        for (int64_t l = 0; l < m.n3; ++l)
          co_yield m.at(i, j, k, l);
}

template <typename T>
// column-major: i3 outer, i0 inner — accesses memory with stride n1*n2*n3
ATTR generator<T> matrix_gen_col(Matrix4D<T>& m) {
  for (int64_t l = 0; l < m.n3; ++l)
    for (int64_t k = 0; k < m.n2; ++k)
      for (int64_t j = 0; j < m.n1; ++j)
        for (int64_t i = 0; i < m.n0; ++i)
          co_yield m.at(i, j, k, l);
}

// ============================================================
// Approach 2: index-tracking iterator (inverted loop control)
// ============================================================
template <typename T> struct ManualRangeRow {
  Matrix4D<T>& m_;

  struct iterator {
    Matrix4D<T>* m_;
    int64_t i0_, i1_, i2_, i3_;

    T& operator*() const { return m_->at(i0_, i1_, i2_, i3_); }

      ATTR iterator& operator++() {
        if (++i3_ < m_->n3) return *this;
        i3_ = 0;
        if (++i2_ < m_->n2) return *this;
        i2_ = 0;
        if (++i1_ < m_->n1) return *this;
        i1_ = 0;
        ++i0_;
        return *this;
      }

    bool operator!=(const iterator& o) const { return i0_ != o.i0_; }
  };

  iterator begin() { return {&m_, 0, 0, 0, 0}; }
  iterator end()   { return {&m_, m_.n0, 0, 0, 0}; }
};

// column-major: i0 varies fastest, i3 outermost
template <typename T> struct ManualRangeCol {
  Matrix4D<T>& m_;

  struct iterator {
    Matrix4D<T>* m_;
    int64_t i0_, i1_, i2_, i3_;

    T& operator*() const { return m_->at(i0_, i1_, i2_, i3_); }

    ATTR iterator& operator++() {
      if (++i0_ < m_->n0) return *this;
      i0_ = 0;
      if (++i1_ < m_->n1) return *this;
      i1_ = 0;
      if (++i2_ < m_->n2) return *this;
      i2_ = 0;
      ++i3_;
      return *this;
    }

    bool operator!=(const iterator& o) const { return i3_ != o.i3_; }
  };

  iterator begin() { return {&m_, 0, 0, 0, 0}; }
  iterator end()   { return {&m_, 0, 0, 0, m_.n3}; }
};

// ============================================================
// Benchmarks
// ============================================================
using data_t = double;

void BM_coro_row(benchmark::State& state) {
  Matrix4D<data_t> m(state.range(0), state.range(1), state.range(2), state.range(3));
  for (auto _ : state) {
    data_t sum = 0;
    for (data_t x : matrix_gen_row(m)) sum += x;
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() *
      state.range(0) * state.range(1) * state.range(2) * state.range(3));
}

void BM_manual_row(benchmark::State& state) {
  Matrix4D<data_t> m(state.range(0), state.range(1), state.range(2), state.range(3));
  for (auto _ : state) {
    data_t sum = 0;
    for (data_t x : ManualRangeRow{m}) sum += x;
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() *
      state.range(0) * state.range(1) * state.range(2) * state.range(3));
}

void BM_coro_col(benchmark::State& state) {
  Matrix4D<data_t> m(state.range(0), state.range(1), state.range(2), state.range(3));
  for (auto _ : state) {
    data_t sum = 0;
    for (data_t x : matrix_gen_col(m)) sum += x;
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() *
      state.range(0) * state.range(1) * state.range(2) * state.range(3));
}

void BM_manual_col(benchmark::State& state) {
  Matrix4D<data_t> m(state.range(0), state.range(1), state.range(2), state.range(3));
  for (auto _ : state) {
    data_t sum = 0;
    for (data_t x : ManualRangeCol{m}) sum += x;
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() *
      state.range(0) * state.range(1) * state.range(2) * state.range(3));
}

#if 0
  ->Args({4, 4, 4, 4})   \
  ->Args({8, 8, 8, 8})   \

#endif // 0 
#define ARG \
  ->Args({16, 16, 16, 16}) \
  ->Args({32, 32, 32, 32}) \
  ->UseRealTime()

BENCHMARK(BM_coro_row)   ARG;
BENCHMARK(BM_coro_col)   ARG;
BENCHMARK(BM_manual_row) ARG;
BENCHMARK(BM_manual_col) ARG;

BENCHMARK_MAIN();
