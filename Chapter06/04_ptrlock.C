#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <iostream>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
inline void spinlock_pause() { _mm_pause(); }
#elif defined(__aarch64__) || defined(_M_ARM64)
inline void spinlock_pause() { __asm__ volatile("yield" ::: "memory"); }
#else
#error "Unsupported architecture"
#endif

#include "benchmark/benchmark.h"

using namespace std;

std::atomic<unsigned long>* pa = new std::atomic<unsigned long>(0);

void BM_atomic(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(pa->fetch_add(1, std::memory_order_relaxed));
  }
  state.SetItemsProcessed(state.iterations());
}

class Spinlock {
  public:
  Spinlock() = default;
  Spinlock(const Spinlock&) = delete;
  Spinlock operator=(const Spinlock&) = delete;
  void lock() {
    static const timespec ns = { 0, 1 };
    for (int i = 0; flag_.load(std::memory_order_relaxed) || flag_.exchange(1, std::memory_order_acquire); ++i) {
      if (i == 8) {
        i = 0;
        nanosleep(&ns, nullptr);
      }
    }
  }
  void unlock() { flag_.store(0, std::memory_order_release); }
  private:
  std::atomic<unsigned int> flag_ {};
};

Spinlock S;
unsigned long* pu = new unsigned long(0);

void BM_spinlock(benchmark::State& state) {
  for (auto _ : state) {
    std::lock_guard L(S);
    benchmark::DoNotOptimize(++*pu);
  }
  state.SetItemsProcessed(state.iterations());
}

class Ptrlock {
  public:
  explicit Ptrlock(unsigned long i) : p_(new unsigned long(i)) {}
  ~Ptrlock() { delete p_.load(std::memory_order_relaxed); }
  unsigned long* lock() {
    static const timespec ns = { 0, 1 };
    unsigned long* p = nullptr;
    for (int i = 0; !p_.load(std::memory_order_relaxed) || !(p = p_.exchange(NULL, std::memory_order_acquire)); ++i) {
      if (i == 8) {
        i = 0;
        nanosleep(&ns, NULL);
      }
    }
    return (p_save_ = p);
  }
  void unlock() { p_.store(p_save_, std::memory_order_release); }
  private:
  std::atomic<unsigned long*> p_;
  unsigned long* p_save_ {};
};

Ptrlock pl(0);

void BM_ptrlock(benchmark::State& state) {
  for (auto _ : state) {
    unsigned long* p = pl.lock();
    benchmark::DoNotOptimize(++*p);
    pl.unlock();
  }
  state.SetItemsProcessed(state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->ThreadRange(1, numcpu) \
  ->UseRealTime()

BENCHMARK(BM_atomic) ARGS;
BENCHMARK(BM_spinlock) ARGS;
BENCHMARK(BM_ptrlock) ARGS;

BENCHMARK_MAIN();
