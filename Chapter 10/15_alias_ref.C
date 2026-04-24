void f1(int& a, int& b, int& x) {
  if (x < 0) x = -x;
  a += x;
  b += x;
}

void f2(int& a, int& b, int& x) {
  if (x < 0) x = -x;
  const int y = x;
  a += y;
  b += y;
}

void f3(int& a, int& b, long& x) {
  if (x < 0) x = -x;
  a += x;
  b += x;
}
