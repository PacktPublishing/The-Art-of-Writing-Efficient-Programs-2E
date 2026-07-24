#include <thread>
#include <iostream>
#include <mutex>

std::mutex iom;

void f(int i) {
  std::lock_guard g(iom);
  std::cout << "Thread " << std::this_thread::get_id() << " i = " << i << " running..." << std::endl;
}

int main() {
  {
    std::thread t(f, 5);
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << t.get_id() << " started" << std::endl;
    }
  }
  std::cout << "Thread ended" << std::endl;
}
