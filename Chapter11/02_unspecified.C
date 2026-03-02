#include <iostream>

int f1() { std::cout << "f1"; return 0; }
int f2() { std::cout << "f2"; return 0; }
void f(int, int) {}

int main() {
  f(f1(), f2());
  std::cout << std::endl;
}
