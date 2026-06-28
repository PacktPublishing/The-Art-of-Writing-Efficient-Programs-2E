#include <iostream>
#include <latch>
#include <thread>
#include <mutex>
#include <chrono>

using namespace std::chrono;
using namespace std::literals::chrono_literals;

int main() {
  std::mutex iom;
  std::latch l(3);
  const auto start = high_resolution_clock::now();
  auto time = [&]() { 
    auto current = high_resolution_clock::now();
    return duration<double>(current - start).count();
  };

  auto work = [&](auto t) {
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << std::this_thread::get_id() << " started at " << time() << 's' << std::endl;
    }
    std::this_thread::sleep_for(t);
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << std::this_thread::get_id() << " waiting at " << time() << 's' << std::endl;
    }
    l.arrive_and_wait();
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << std::this_thread::get_id() << " done at " << time() << 's' << std::endl;
    }
  };

  std::jthread t1(work, 500ms);
  std::jthread t2(work, 3s);
  std::jthread t3(work, 5s);
}
