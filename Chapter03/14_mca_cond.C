#include <stdlib.h>
#define MCA_START __asm volatile("# LLVM-MCA-BEGIN")
#define MCA_END __asm volatile("# LLVM-MCA-END")

size_t N;
const unsigned long* p1;
const unsigned long* p2;
const int* b1;
unsigned long a1 = 0;
unsigned long a2 = 0;

void add_multiply() {
    for (size_t i = 0; i < N; ++i) {
MCA_START;
        if (p1[i] > p2[i]) {
            a1 += p1[i] + p2[i];
        } else {
            a2 += p1[i] * p2[i];
        }
MCA_END;
    }
}
