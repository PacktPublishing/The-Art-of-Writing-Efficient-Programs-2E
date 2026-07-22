#ifndef INCLUDED_SPINLOCK_H
#define INCLUDED_SPINLOCK_H
#include <time.h>
#include <atomic>
namespace {
  static const struct timespec spin_wait_short = { 0, 1 };
  static const struct timespec spin_wait_long  = { 0, 10000001 };
  static inline void spin_wait_short_sleep() { nanosleep(&spin_wait_short, nullptr); }
  static inline void spin_wait_long_sleep()  { nanosleep(&spin_wait_long,  nullptr); }
}

class SpinLock {
  public:
  void lock() {
    for (int spin_count = 0;
        lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire);
        ++spin_count)
    {
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
      if (spin_count < 8) {
        spin_wait_short_sleep();
      } else {
        spin_count = 0;
        spin_wait_long_sleep();
      }
    }
  }

  void unlock() {
    lock_.store(0, std::memory_order_release);
  }

  bool try_lock() {
    for (int spin_count = 0;
        lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire);
        ++spin_count)
    {
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
      if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
      if (spin_count < 8) {
        spin_wait_short_sleep();
      } else {
        return false;
      }
    }
    return true;
  }

  bool locked() const {
    return lock_.load(std::memory_order_relaxed) == 1;
  }

  private:
  std::atomic<int> lock_ {0};
};

#endif // INCLUDED_SPINLOCK_H
