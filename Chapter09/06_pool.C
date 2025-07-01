#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <cstddef>
#include <iostream>
#include <fstream>
#include <atomic>

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

class Pool {
    static constexpr size_t capacity_ { 1UL << 46 };
    std::byte* memory_ { static_cast<std::byte*>(::mmap(nullptr, capacity_, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0 )) };
    std::atomic<std::size_t> size_ { 0 };

    public:
    Pool() = default;
    ~Pool() { ::munmap(memory_, capacity_); }
    size_t capacity() const { return capacity_; }
    size_t size() const { return size_; }
    void* allocate(size_t s) { return memory_ + size_.fetch_add(s); }
};

int main() {
    Pool pool;
    for (size_t i = 0; i != 4096; ++i) {
        constexpr size_t size = 1UL << 16;
        void* p = pool.allocate(size);
        memset(p, 0xab, size);
        if ((i % 128) == 0) std::cout << i << ": RSS: " << get_rss() << "MB" << std::endl;
    }
}
