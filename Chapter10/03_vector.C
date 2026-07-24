#include <cstdlib>
#include <vector>

void fill(int* p, size_t n);
void work(int* p, size_t n);

void f() {
    constexpr size_t N = 16;
    std::vector<int> v(N);
    fill(v.data(), N); // fill v with data
    for (int& x : v) ++x;
    work(v.data(), N); // use v for computations
}
