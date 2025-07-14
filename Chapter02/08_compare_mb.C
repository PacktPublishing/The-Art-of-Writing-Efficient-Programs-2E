// Benchmark for comparison functions used in 01-04.
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::system_clock;
using std::cout;
using std::endl;
using std::unique_ptr;

bool compare1(const char* s1, const char* s2) {
    if (s1 == s2) return false;
    for (unsigned int i = 0; ; ++i) {
        if (s1[i] != s2[i]) return s1[i] > s2[i];
    }
    return false;
}

bool compare2(const char* s1, const char* s2) {
    if (s1 == s2) return false;
    for (int i = 0; ; ++i) {
        if (s1[i] != s2[i]) return s1[i] > s2[i];
    }
    return false;
}

auto usec(auto t) {
    return duration_cast<microseconds>(t).count();
}

int main() {
    constexpr unsigned int N = 1 << 20;
    constexpr int NI = 1 << 11;
    unique_ptr<char[]> s(new char[2*N]);
    ::memset(s.get(), 'a', 2*N*sizeof(char));
    s[2*N-1] = 0;
    auto t0 = system_clock::now();
    for (int i = 0; i < NI; ++i) {
        compare1(s.get(), s.get() + N);
    }
    auto t1 = system_clock::now();
    for (int i = 0; i < NI; ++i) {
        compare2(s.get(), s.get() + N);
    }
    auto t2 = system_clock::now();
    cout << usec(t1 - t0) << "us " << usec(t2 - t1) << "us" << endl;
}
