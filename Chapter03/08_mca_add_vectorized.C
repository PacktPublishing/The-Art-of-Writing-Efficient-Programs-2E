// clang 08_mca_add_vectorized.C -O3  -march=native -S
// Edit 08_mca_add_vectorized.s, delete everything except the hot loop
// llvm-mca -mcpu=native -iterations=100 -timeline 08_mca_add_vectorized.s
//
// For Graviton 4: 
// clang --target=aarch64-linux-gnu -mcpu=neoverse-v2 ; llvm-mca -mtriple=aarch64-linux-gnu -mcpu=neoverse-v2
// For Apple M3:
// clang --target=aarch64-apple-darwin -mcpu=apple-m3 ; llvm-mca -mtriple=aarch64-apple-darwin -mcpu=apple-m3
// For Apple M4:
// clang --target=aarch64-apple-darwin -mcpu=apple-revere ; llvm-mca -mtriple=aarch64-apple-darwin -mcpu=apple-revere
unsigned long N;
const unsigned long* p1;
const unsigned long* p2;
const int* b1;
unsigned long a1 = 0;
unsigned long a2 = 0;

void add() {
    for (unsigned long i = 0; i < N; ++i) {
        a1 += p1[i] + p2[i];
    }
}

