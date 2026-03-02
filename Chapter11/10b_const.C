void g(int y);
bool f(int x) {
  g(x + 1);
  return x + 1 > x;
}

