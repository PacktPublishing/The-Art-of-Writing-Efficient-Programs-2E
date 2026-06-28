#include <atomic>

struct l2 { long i, j;};
static_assert(alignof(l2) == 8);
using al2 = std::atomic<l2>;
static_assert(alignof(al2) == 16);
using ral2 = std::atomic_ref<l2>;
static_assert(ral2::required_alignment == 16);

#include <iostream>

struct data {
  long pad = 0;
  l2 ij = { 1, 2 };
  l2 kl = { 3, 4 };
};
static_assert(sizeof(data) == sizeof(long) + sizeof(l2)*2);

int main() {
  alignas(16) data d;
  std::cout << "Alignment of pad: " << &d.pad << std::endl;
  std::cout << "Alignment of ij: " << &d.ij << std::endl;
  std::cout << "Alignment of kl: " << &d.kl << std::endl;
  ral2 rij(d.kl);
  rij.exchange({3, 4});
  rij.compare_exchange_strong(d.kl, {5, 6});
}
