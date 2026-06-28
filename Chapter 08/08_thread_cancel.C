#include <thread>
#include <stop_token>
#include <iostream>
#include <mutex>
#include <chrono>

using namespace std::literals::chrono_literals;

std::mutex iom;

void f(std::stop_token stop_token, int i)
{
  {
    std::lock_guard g(iom);
    std::cout << "Thread " << std::this_thread::get_id() << " i = " << i << " " << std::flush;
  }
  while (!stop_token.stop_requested())
  {
    {
      std::lock_guard g(iom);
      std::cout << ++i << ' ' << std::flush;
    }
    std::this_thread::sleep_for(300ms);
  }
  {
    std::lock_guard g(iom);
    std::cout << "stop requested" << std::endl;
  }
}

int main() {
  {
    std::jthread t(f, 5);
    {
      std::lock_guard g(iom);
      std::cout << "Thread " << t.get_id() << " started" << std::endl;
    }
    std::this_thread::sleep_for(3s);
    // The destructor of jthread calls request_stop() and join()
  }
  std::cout << "Thread ended" << std::endl;
}
