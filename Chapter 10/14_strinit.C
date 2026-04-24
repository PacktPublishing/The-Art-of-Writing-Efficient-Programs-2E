#include <stdlib.h>
#include <string.h>

void init1(char* a, char* b, size_t N) {
  for (size_t i = 0; i < N; ++i) {
    a[i] = '0';
    b[i] = '1';
  }
}

void init2(char* __restrict a, char* __restrict b, size_t N) {
  for (size_t i = 0; i < N; ++i) {
    a[i] = '0';
    b[i] = '1';
  }
}

void init3(char* a, char* b, size_t N) {
  memset(a, '0', N);
  memset(b, '1', N);
}
