#include <cstdlib>
#include <vector>
#include <utility>

void fill(std::vector<int>& v);
void work(std::vector<int>& v);

constexpr size_t N = 16;
void f() {
    std::vector<int> v(N);
    fill(v); // fill v with data
    const size_t n = v.size();
    [[assume(n == N)]];
    for (int& x : v) ++x;
    work(v); // use v for computations
}
