// Sequential memory access implementation, uses dual buffers.
#include "string_edit_common.h"
#include "memory_info.h"
#include "memory_info.C" // To simplify build process

#include <chrono>
#include <iostream>
#include <regex>
#include <string>

int main() {
    char* buf = static_cast<char*>(malloc(std::max(1UL << 20, N*(L + 10*M))));
    {
        char* s = buf;
        for (size_t i = 0; i < N; ++i) {
            s = make_random_string(s, L);
            *(s++) = '\0';
        }
    }
    if (debug) {
        std::cout << "Initial:\n";
        const char* s = buf;
        for (size_t i = 0; i < N; ++i) {
            std::cout << s << std::endl;
            s += strlen(s) + 1;
        }
    }

    auto t0 = std::chrono::steady_clock::now();

    size_t num_strings = 0, num_changes = 0;
    size_t max_rss = 0;
    ssize_t dL = Lm > 1 ? 1 : 0;
    for (size_t i = 0; i < M; ++i) {
        std::regex r; std::string fmt;
        std::tie(r, fmt) = make_random_change(Lm, dL);
        char* buf1 = static_cast<char*>(malloc(std::max(1UL << 20, N*(L + 10*M))));
        const char* s = buf;
        char* s1 = buf1, *s2;
        for (size_t j = 0; j < N; ++j) {
            const size_t ls = strlen(s) + 1;
            if ((s2 = edit(s, s + ls, s1, r, fmt)) != s1) {
                if (debug) std::cout << s << " -> " << s1 << std::endl;
                s1 = s2;
                ++num_changes;
            } else {
                memcpy(s1, s, ls);
                s1 += ls;
            }
            s += ls;
            ++num_strings;
        }
        max_rss = std::max(MemoryInfo().Used().rss, max_rss);
        free(buf);
        buf = buf1;
        if (i && (i & (MP - 1)) == 0) {
            auto t1 = std::chrono::steady_clock::now();
            std::chrono::duration<double> dt = t1 - t0;
            std::cout << i << ": " << dt.count() << ", change fraction: " << double(num_changes)/num_strings << "(" << num_changes << "/" << num_strings << ")" << ", max RSS: " << max_rss/1024 << "KB" << std::endl;
        }
    }

    if (debug) {
        std::cout << "\nFinal:\n";
        const char* s = buf;
        for (size_t i = 0; i < N; ++i) {
            std::cout << s << std::endl;
            s += strlen(s) + 1;
        }
    }

    free(buf);

    auto t1 = std::chrono::steady_clock::now();
    std::chrono::duration<double> dt = t1 - t0;
    std::cout << "Elapsed time: " << dt.count() << ", change fraction: " << double(num_changes)/num_strings << "(" << num_changes << "/" << num_strings << ")" << ", max RSS: " << max_rss/1024 << "KB" << std::endl;
}
