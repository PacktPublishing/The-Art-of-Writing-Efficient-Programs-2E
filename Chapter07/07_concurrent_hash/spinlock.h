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
