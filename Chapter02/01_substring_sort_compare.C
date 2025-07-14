// Comparison function for substring sort 01_substring_sort.C.
bool compare(const char* s1, const char* s2, unsigned int l) {
    if (s1 == s2) return false;
    for (unsigned int i = 0; i < l; ++i) {
        if (s1[i] != s2[i]) return s1[i] > s2[i];
    }
    return false;
}

