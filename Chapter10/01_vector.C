#include <cstdlib>
#include <vector>

void fill(std::vector<int>& v);
void work(std::vector<int>& v);

void f() {
    constexpr size_t N = 16;
    std::vector<int> v(N);
    fill(v); // fill v with data
    for (int& x : v) ++x;
    work(v); // use v for computations
}
