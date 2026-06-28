#include <atomic>
void f(int& i) { ++i; }
void g(int& i) { std::atomic_ref ai(i); ++ai; }
void h(std::atomic<int>& i) { ++i; }
