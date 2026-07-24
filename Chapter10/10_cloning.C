#include <algorithm>

inline bool pred(int a, int b) { return a > b; }

void sort1(int* a, int* b) {
  std::sort(a, b, pred);
}

void sort2(int* a, int* b) {
  std::sort(a, b, [](int a, int b) { return a > b; });
}

void sort3(int* a, int* b) {
  std::sort(a, b, [](int a, int b) { return pred(a, b); });
}
