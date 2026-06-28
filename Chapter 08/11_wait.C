#include <iostream>
#include <latch>
#include <thread>
#include <mutex>
#include <chrono>

using namespace std::chrono;
using namespace std::literals::chrono_literals;

int main() {
  std::mutex iom;
  std::atomic<int> i(0);
  const auto start = high_resolution_clock::now();
  auto time = [&]() { 
    auto current = high_resolution_clock::now();
    return duration<double>(current - start).count();
  };

  auto work = [&]() {
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << std::this_thread::get_id() << " started at " << time() << 's' << std::endl;
    }
    i.wait(0);
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << std::this_thread::get_id() << " done at " << time() << 's' << std::endl;
    }
  };

  std::jthread t1(work);
  std::jthread t2(work);
  std::jthread t3(work);
  std::this_thread::sleep_for(1s);
  {
    std::lock_guard g(iom);
    std::cout << "Changing i at " << time() << 's' << std::endl;
  }
  i = 1;
  std::this_thread::sleep_for(1s);
  {
    std::lock_guard g(iom);
    std::cout << "Notifying i at " << time() << 's' << std::endl;
  }
  i.notify_all();
}
