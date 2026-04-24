#include <cstdlib>
#include <vector>
#include <utility>

void fill(std::vector<int>& v);
void work(std::vector<int>& v);

constexpr size_t N = 16;
void f() {
    std::vector<int> v(N);
    fill(v); // fill v with data
    #pragma clang loop unroll(full)
    for (size_t i = 0; i != v.size(); ++i) ++v[i];
    work(v); // use v for computations
}
