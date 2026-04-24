#include <cstdlib>
#include <cstdint>
#include <vector>

void g1(std::vector<char>& v) {
    char* const p = v.data();
    size_t const n = v.size();
    for (size_t i = 0; i != n; ++i) p[i] = 0;
}
