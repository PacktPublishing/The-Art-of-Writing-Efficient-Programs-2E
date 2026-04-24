inline double g(double x) { return x + 1; }
inline double h(double x) { return x - 1; }

double f(int& i, double x) {
  double res = g(x);
  ++i;
  res += h(x);
  res += g(x);
  ++i;
  res += h(x);
  return res;
}
