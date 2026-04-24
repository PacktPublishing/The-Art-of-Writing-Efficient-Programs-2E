#include <cstdlib>
#include <cstdint>
#include <vector>

void f2(std::vector<int>& v) {
    for (size_t i = 0; i != v.size(); ++i) v[i] = 0;
}
