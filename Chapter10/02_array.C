#include <cstdlib>

void fill(int* p, size_t n);
void work(int* p, size_t n);

constexpr size_t N = 16;
void f() {
    int v[N];
    fill(v, N); // fill v with data
    for (int& x : v) ++x;
    work(v, N); // use v for computations
}
