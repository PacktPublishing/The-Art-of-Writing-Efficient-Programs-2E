#include <type_traits>
#include <vector>
#include <memory>

void fi(std::vector<int>* v) { std::destroy_at(v); }

struct S1 {
  long a;
  double b;
};
void f1(std::vector<S1>* v) { std::destroy_at(v); }

struct S2 {
  long a;
  double b;
  ~S2() = default;
};
void f2(std::vector<S2>* v) { std::destroy_at(v); }
static_assert(std::is_trivially_destructible_v<S2>);

struct S3 {
  long a;
  double b;
  ~S3() {}
};
void f3(std::vector<S3>* v) { std::destroy_at(v); }
static_assert(!std::is_trivially_destructible_v<S3>);

struct S4 {
  long a;
  double b;
  ~S4();
};
void f4(std::vector<S4>* v) { std::destroy_at(v); }
static_assert(!std::is_trivially_destructible_v<S4>);
