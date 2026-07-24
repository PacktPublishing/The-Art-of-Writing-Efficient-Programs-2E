#include <vector>

int g(int a);
class C {
    public:
    explicit operator bool() const;
};

template <typename T>
int f1(const std::vector<int>& v, const T& t) {
  const bool b(t);
  int sum = 0;
  for (int a : v) {
    if (b) sum += g(a);
  }
  return sum;
} 
template int f1<C>(const std::vector<int>&, const C&);

template <typename T>
int f2(const std::vector<int>& v, const T& t) {
  int sum = 0;
  for (int a : v) {
    if (t) sum += g(a);
  }
  return sum;
} 
template int f2<C>(const std::vector<int>&, const C&);

