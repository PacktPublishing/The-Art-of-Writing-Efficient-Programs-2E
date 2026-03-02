#include <iostream>
#include <limits.h>

inline int f(int k) {
    return k + 10;
}
int g(int k) {
    int res = f(k);
    if (k > INT_MAX - 5) return res;
    return 42;
}

int main() {
    std::cout << INT_MAX << ": ";
    int k;
    std::cin >> k;
    int res = g(k);
    if (res != 42) std::cout << "Large k" << std::endl;
    std::cout << res << std::endl;
}
