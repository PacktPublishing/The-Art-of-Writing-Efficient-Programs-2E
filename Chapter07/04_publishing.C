#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

int main() {
  // Root pointer to all data.
  std::atomic<int*> data{};
  int n = 0;

  // Fire up a the publishing thread: it creates the data then makes it accessible via the root pointer.
  std::jthread publisher([&]() {
      std::cout << "Preparing data" << std::endl;
      std::this_thread::sleep_for(2000ms);
      n = 10;
      int* p = new int[n];
      for (int i = 0; i != n; ++i) p[i] = (i > 1) ? (p[i - 1] + p[i - 2]) : 1;
      std::cout << "Publishing data" << std::endl;
      data.store(p, std::memory_order_release);
      });

  // Wait for the data to become available.
  std::cout << "Waiting for data" << std::endl;
  while (true) {
    if (int* p = data.load(std::memory_order_acquire); p) {
      for (int i = 0; i != n; ++i) {
        std::cout << p[i] << " ";
      }
      std::cout << std::endl;
      break;
    }
  }
}
