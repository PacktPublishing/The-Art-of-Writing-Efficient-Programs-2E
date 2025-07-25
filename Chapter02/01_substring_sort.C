// Substring sort
// Build as follows:
// $CXX 01_substring_sort.C 01_substring_sort_compare.C -g -O3 -mavx2 -I. -o 01_substring_sort
//   (On ARM or recent X86 CPUs, use different architecture, for example, -march=native)
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::system_clock;
using std::cout;
using std::endl;
using std::minstd_rand;
using std::unique_ptr;
using std::vector;

bool compare(const char* s1, const char* s2, unsigned int l);

int main() {
#include "00_substring_sort_prep.C"

    size_t count = 0;
    std::sort(vs.begin(), vs.end(), [&](const char* a, const char* b) { ++count; return compare(a, b, L); });
    system_clock::time_point t2 = system_clock::now();
    cout << "Sort time: " << duration_cast<milliseconds>(t2 - t1).count() << "ms (" << count << " comparisons)" << endl;
}
