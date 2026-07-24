#include <iostream>
#include <semaphore>
#include <thread>
#include <mutex>
#include <chrono>

using namespace std::chrono;
using namespace std::literals::chrono_literals;

int main() {
  std::mutex iom;
  std::counting_semaphore<2> sem(2);   // max 2, start with 2 permits
  const auto start = high_resolution_clock::now();
  auto time = [&]() {
    auto current = high_resolution_clock::now();
    return duration<double>(current - start).count();
  };

  auto work = [&](auto delay, int id) {
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << id << " started at " << time() << 's' << std::endl;
    }
    std::this_thread::sleep_for(delay);   // stagger arrival at the door
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << id << " waiting at " << time() << 's' << std::endl;
    }
    sem.acquire();                        // bottleneck: only 2 get through
    {
      auto t = time();                    // Outside the lock
      std::lock_guard g(iom);
      std::cout << "Thread " << id << " ENTERED at " << t << 's' << std::endl;
    }
    std::this_thread::sleep_for(2s);      // time spent inside the section
    {
      auto t = time();                    // Outside the lock
      sem.release();
      std::lock_guard g(iom);
      std::cout << "Thread " << id << " LEAVING at " << t << 's' << std::endl;
    }
  };

  std::jthread t1(work, 0ms  , 1);
  std::jthread t2(work, 100ms, 2);
  std::jthread t3(work, 300ms, 3);
  std::jthread t4(work, 500ms, 4);
  std::jthread t5(work, 700ms, 5);
}
