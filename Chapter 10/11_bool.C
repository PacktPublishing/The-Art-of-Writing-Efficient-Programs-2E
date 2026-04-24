#include <vector>

int g(int a);
int f1(const std::vector<int>& v, bool b) {
  int sum = 0;
  for (int a : v) {
    if (b) sum += g(a);
  }
  return sum;
} 

int f2(const std::vector<int>& v, bool b) {
  if (!b) return 0;
  int sum = 0;
  for (int a : v) {
    sum += g(a);
  }
  return sum;
} 
