// Comparison function for substring sort 02_substring_sort.C.
// 01 optimized to omit unnecessary bount check in compare()
bool compare(const char* s1, const char* s2) {
    if (s1 == s2) return false;
    for (unsigned int i = 0; ; ++i) {
        if (s1[i] != s2[i]) return s1[i] > s2[i];
    }
    return false;
}

