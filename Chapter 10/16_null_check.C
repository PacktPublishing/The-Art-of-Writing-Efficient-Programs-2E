#include <algorithm>

template <typename T>
void my_swap(T* p, T* q) {
  if (p && q) {
    using std::swap;
    swap(*p, *q);
  }
}

template <typename T>
void my_swap_nocheck(T* p, T* q) {
  using std::swap;
  swap(*p, *q);
}

void f1(int* p, int* q) {
    if (p && q) my_swap(p, q);
}

void f2(int* p, int* q) {
    if (p && q) my_swap_nocheck(p, q);
}
