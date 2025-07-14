// Comparison function for substring sort 03_substring_sort.C.
// 02 with int instead of unsigned int.
bool compare(const char* s1, const char* s2) {
    if (s1 == s2) return false;
    for (int i = 0; ; ++i) {
        if (s1[i] != s2[i]) return s1[i] > s2[i];
    }
    return false;
}

