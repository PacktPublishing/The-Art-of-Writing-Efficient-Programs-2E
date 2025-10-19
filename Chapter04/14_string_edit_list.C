// Straightforward implementation - list of strings.
#include "string_edit_common.h"
#include "memory_info.h"
#include "memory_info.C" // To simplify build process

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
#include <list>

int main() {
    std::list<std::string> data;
    for (size_t i = 0; i < N; ++i) data.push_back(make_random_string(L));
    if (debug) {
        std::cout << "Initial:\n";
        for (auto s : data) std::cout << s << std::endl;
    }

    auto t0 = std::chrono::steady_clock::now();

    size_t num_strings = 0, num_changes = 0;
    size_t max_rss = 0;
    ssize_t dL = Lm > 1 ? 1 : 0;
    for (size_t i = 0; i < M; ++i) {
        std::regex r; std::string fmt;
        std::tie(r, fmt) = make_random_change(Lm, dL);
        for (auto it = data.begin(), it0 = --data.end(), it1 = it; true; it = it1) {
            it1 = it;
            ++it1;
            const bool done = it == it0;
            std::string s1;
            bool changed = edit(*it, s1, r, fmt);
            if (changed) {
                if (debug) std::cout << *it << " -> " << s1 << std::endl;
                data.insert(it, s1);
                data.erase(it);
                ++num_changes;
            }
            ++num_strings;
            if (done) break;
        }
        max_rss = std::max(MemoryInfo().Used().rss, max_rss);
        if (i && (i & (MP - 1)) == 0) {
            auto t1 = std::chrono::steady_clock::now();
            std::chrono::duration<double> dt = t1 - t0;
            std::cout << i << ": " << dt.count() << ", change fraction: " << double(num_changes)/num_strings << "(" << num_changes << "/" << num_strings << ")" << ", max RSS: " << max_rss/1024 << "KB" << std::endl;
        }
    }

    if (debug) {
        std::cout << "\nFinal:\n";
        for (auto s : data) std::cout << s << std::endl;
    }

    auto t1 = std::chrono::steady_clock::now();
    std::chrono::duration<double> dt = t1 - t0;
    std::cout << "Elapsed time: " << dt.count() << ", change fraction: " << double(num_changes)/num_strings << "(" << num_changes << "/" << num_strings << ")" << ", max RSS: " << max_rss/1024 << "KB" << std::endl;
}
