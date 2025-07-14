#include <math.h>
#include <time.h>
#include <unistd.h>

#include <iostream>
#include <future>
#include <mutex>
#include <string>
#include <string_view>

using std::cout;
using std::endl;

template <int Clock> class Timer {
    timespec t0_;

    public:
    Timer() { ::clock_gettime(Clock, &t0_); }
    ~Timer() = default;
    double time() const {
        timespec t1;
        clock_gettime(Clock, &t1);
        return t1.tv_sec - t0_.tv_sec + 1e-9*(t1.tv_nsec - t0_.tv_nsec);
    }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
};

class Timers {
    Timer<CLOCK_REALTIME> rt_;
    Timer<CLOCK_PROCESS_CPUTIME_ID> ct_;
    Timer<CLOCK_THREAD_CPUTIME_ID> tt_;
    const std::string msg_;

    public:
    Timers(std::string_view msg) : msg_(msg) {}
    ~Timers() {
        cout.precision(4);
        cout << msg_ <<
            ": Real time: " << rt_.time() <<
            "s, CPU time: " << ct_.time() <<
            "s, Thread time: " << tt_.time() << "s" <<
            endl;
    }
};

int main() {
    constexpr double X = 1e7;

    // Busy time
    {
        Timers t("Computing");
        double s = 0;
        for (double x = 0; x < X; x += 0.1) s += sin(x);
        cout << "Result " << s << endl;
    }

    // Idle time
    {
        Timers t("Idle");
        sleep(1);
    }


    // Thread time
    {
        Timers t("Thread Computing");
        double s = 0;
        auto f = std::async(std::launch::async, [&]{ for (double x = 0; x < X; x += 0.1) s += sin(x); });
        f.wait();
        cout << "Result " << s << endl;
    }

    // Threads time
    {
        Timers t("Threads Computing");
        double s1 = 0, s2 = 0;
        auto f = std::async(std::launch::async, [&]{ for (double x = 0; x < X; x += 0.1) s1 += sin(x); });
        for (double x = 0; x < X; x += 0.1) s2 += sin(x);
        f.wait();
        cout << "Result " << s1 << " " << s2 << endl;
    }
}
