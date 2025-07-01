#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <cstddef>
#include <iostream>
#include <fstream>

double get_rss() {
    std::ifstream statm_file("/proc/self/statm");
    if (!statm_file.is_open()) return -1;
    long size, resident;//, share, text, lib, data, dt;
    statm_file >> size >> resident;// >> share >> text >> lib >> data >> dt;
    return resident*getpagesize()/1024./1024.;
}

double get_max_rss() {
    struct rusage ru;
    if (::getrusage(RUSAGE_SELF, &ru) == 0) return ru.ru_maxrss/1024.;
    else return -1;
}

constexpr size_t capacity = 1UL << 46;

int main() {
    std::cout << "Initial RSS: " << get_rss() << "MB" << std::endl;

    std::byte* p = static_cast<std::byte*>(::mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0));

    std::cout << "Reserving " << capacity/1024/1024/1024/1024 << "TB ";
    if (p != MAP_FAILED) std::cout << "succeeded at address " << p << std::endl;
    else { std::cout << "failed" << std::endl; return 1; }
    
    std::cout << "Mapped RSS: " << get_rss() << "MB" << std::endl;

    constexpr size_t size = 1UL << 30;
    ::memset(p, 0x1b, size);
    std::cout << "Used " << size/1024/1024/1024 << "GB, RSS: " << get_rss() << "MB" << std::endl;

    ::memset(p + 2*size, 0x1b, size);
    std::cout << "Used " << size/1024/1024/1024 << "GB, RSS: " << get_rss() << "MB" << std::endl;

    ::madvise(p, size, MADV_DONTNEED);
    std::cout << "Released " << size/1024/1024/1024 << "GB, RSS: " << get_rss() << "MB" << std::endl;

    std::cout << "Final RSS: " << get_rss() << "MB " << ", Max RSS: " << get_max_rss() << "MB" << std::endl;

    ::getc(stdin);

    ::munmap(p, capacity);
}
