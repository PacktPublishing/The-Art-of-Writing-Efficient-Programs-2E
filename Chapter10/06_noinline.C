double g(double x);
double h(double x);

double f(int& i, double x) {
  double res = g(x);
  ++i;
  res += h(x);
  res += g(x);
  ++i;
  res += h(x);
  return res;
}
