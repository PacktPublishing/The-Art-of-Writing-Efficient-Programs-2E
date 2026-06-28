#include <atomic>
#include <cmath>
#include <condition_variable>
#include <coroutine>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

struct task {
  struct promise_type {
    task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};

// Shared state that both coroutines will use to communicate and steal work from
// each other.
struct SharedState {
  std::mutex m;
  std::condition_variable cv;           // Used only to suspend the main thread until all work is done

  // waiting[i] is true if worker 'i' has run out of work and is suspended.
  // It's atomic so the active worker can do a lock-free check before taking the
  // mutex.
  std::atomic<bool> waiting[2] = {false, false};

  // The range of work [remaining_begin, remaining_end) for each worker.
  int remaining_begin[2] = {0, 0};
  int remaining_end[2] = {0, 0};

  // The coroutine handles. When a worker goes idle, it stores its handle here.
  std::coroutine_handle<> waiting_coro[2];

  // We maintain ownership of the threads running the coroutines.
  std::unique_ptr<std::jthread> threads[2];

  // Flag to mark that a worker has completely finished and exited the
  // coroutine.
  bool finished[2] = {false, false};

  // Toggle for the benchmark
  bool enable_stealing = true;
}; // SharedState

// A custom Awaitable. This is where the magic happens for pausing and resuming
// threads. Unlike co_yield which just returns a value, this awaitable suspends
// the coroutine and dictates how/when it should be woken up.
struct AwaitWork {
  SharedState &state;
  int id; // The ID of the worker requesting work

  // await_ready runs first. If it returns true, we don't suspend at all.
  // We always want to try to suspend and check the state, so we return false.
  bool await_ready() { return false; }

  // await_suspend is called AFTER the coroutine is suspended.
  // The return type is bool.
  // Returning 'true' keeps the coroutine suspended and returns control to the
  // caller/thread. Returning 'false' immediately resumes the coroutine without
  // giving up the thread.
  bool await_suspend(std::coroutine_handle<> h) {
    std::lock_guard lk(state.m);

    // Scenario 1: The OTHER worker is already finished.
    // There is no one left to steal from, so we should just exit immediately.
    if (state.finished[1 - id]) {
      state.remaining_begin[id] = 0;
      state.remaining_end[id] = 0;
      state.finished[id] = true;
      state.cv.notify_all(); // Wake up main thread
      return false;          // Don't suspend! Resume immediately and exit.
    }

    // Scenario 2: The OTHER worker is ALSO waiting for work.
    // If both of us are out of work, the entire workload is globally finished.
    if (state.waiting[1 - id]) {
      state.waiting[1 - id] = false;
      state.remaining_begin[1 - id] = 0;
      state.remaining_end[1 - id] = 0;

      // Grab the other suspended coroutine handle
      const auto other_h = state.waiting_coro[1 - id];

      // We must wake up the OTHER coroutine so it can exit properly.
      // We do this by spawning a new thread that simply calls resume().
      state.threads[1 - id] =
          std::make_unique<std::jthread>([other_h] { other_h.resume(); });

      // And for ourselves, we set our bounds to 0 and exit immediately.
      state.remaining_begin[id] = 0;
      state.remaining_end[id] = 0;
      state.finished[id] = true;
      state.cv.notify_all();
      return false; // Don't suspend! Resume immediately and exit.
    }

    // Scenario 3: The OTHER worker is still busy.
    // We save our coroutine handle so the busy worker can wake us up when it
    // splits its work.
    state.waiting_coro[id] = h;
    state.waiting[id] = true;

    // Return true to officially suspend.
    // Our OS thread will now return from the `resume()` call and gracefully
    // die!
    return true;
  }

  // await_resume is called right after the coroutine wakes up.
  // We don't need it to return anything, our new bounds will be found in
  // SharedState.
  void await_resume() {}
}; // AwaitWork

constexpr int CHUNK_SIZE = 1000;
constexpr int THRESHOLD = 2000;

// Compute-intensive work
double work(double x, int id) {
  for (size_t i = 0; i != 10; ++i) {
    x = std::sin(std::cos(x)) + std::cos(std::sin(x));
  }
  // Artificially slow down worker 1 so we guarantee stealing happens
  if (id == 1) {
    for (size_t i = 0; i != 10; ++i) {
      x = std::sin(std::cos(x)) + std::cos(std::sin(x));
    }
  }
  return x;
}

// The coroutine worker. It processes chunks of data and cooperatively
// yields/steals work.
task coro(int id, int begin, int end, SharedState &state) {
  volatile double v; (void)v;
  while (true) {

    // 1. COMPUTE LOOP
    while (begin < end) {
      const int chunk = std::min(begin + CHUNK_SIZE, end);
      for (int i = begin; i < chunk; ++i) {
        v = work(i, id);
      }
      begin = chunk;

      // 2. COOPERATIVE WORK SHARING
      // Lock-free check first for speed: does the other worker need work?
      if (state.enable_stealing &&
          state.waiting[1 - id].load(std::memory_order_relaxed)) {
        std::lock_guard lk(state.m);
        // Double check with lock
        if (state.waiting[1 - id]) {
          const int rem = end - begin;
          // Only share if we have a meaningful amount of work left
          if (rem > THRESHOLD) {
            const int steal = rem/2;
            end -= steal; // Shrink our own bounds

            // Give the stolen bounds to the idle worker
            state.remaining_begin[1 - id] = end;
            state.remaining_end[1 - id] = end + steal;
            state.waiting[1 - id] = false;

            // Grab the idle worker's suspended coroutine handle
            const auto h = state.waiting_coro[1 - id];

            // The Magic: We resume the idle worker's coroutine on a completely
            // NEW thread! This perfectly replaces the OS thread that died when
            // the idle worker suspended, keeping our total hardware threads
            // bounded and avoiding thread pool managers.
            state.threads[1 - id] =
              std::make_unique<std::jthread>([h] { h.resume(); });
          }
        }
      }
    } // end of chunk loop

    // 3a. OUT OF WORK - no stealing.
    if (!state.enable_stealing) {
      std::lock_guard lk(state.m);
      state.finished[id] = true;
      state.cv.notify_all();
      break; // Exit the coroutine completely
    }

    // 3b. OUT OF WORK - with stealing.
    // We ran out of work. Suspend our coroutine using our custom Awaitable!
    // This will park our coroutine in memory and kill our current OS thread.
    co_await AwaitWork{state, id};

    // When we wake up here, we are on a BRAND NEW OS THREAD!
    // The other worker gave us a new range of work to compute.
    // Note: We do NOT need to lock the mutex to read these values. The other
    // worker modified them BEFORE creating the new `std::jthread` that resumed us. 
    // Thread creation establishes a strict "happens-before" relationship in C++, 
    // guaranteeing that those modifications are fully visible to this new thread.
    begin = state.remaining_begin[id];
    end = state.remaining_end[id];

    // If we woke up with no work, it means the entire job is done. Time to
    // exit.
    if (begin >= end) {
      std::lock_guard lk(state.m);
      state.finished[id] = true;
      state.cv.notify_all();
      break; // Exit the coroutine completely
    }
  }
} // coro()

void run_test(bool enable_stealing) {
  std::cout << "\n--- Running test " << (enable_stealing ? "WITH" : "WITHOUT")
            << " stealing ---\n";
  constexpr int N = 500000; // Increased workload for timing
  SharedState state;
  state.enable_stealing = enable_stealing;

  const auto start = std::chrono::high_resolution_clock::now();

  // Launch the initial two coroutines on two separate threads
  state.threads[0] =
      std::make_unique<std::jthread>([&] { coro(0, 0, N, state); });
  state.threads[1] =
      std::make_unique<std::jthread>([&] { coro(1, N, 2*N, state); });

  // Main thread blocks here until both workers signal they have reached the end
  std::unique_lock lk(state.m);
  state.cv.wait(lk, [&] { return state.finished[0] && state.finished[1]; });

  const auto end = std::chrono::high_resolution_clock::now();
  const std::chrono::duration<double, std::milli> elapsed = end - start;
  std::cout << "Test " << (enable_stealing ? "WITH" : "WITHOUT")
            << " stealing completed in " << elapsed.count() << " ms.\n";
} // run_test()

int main() {
  run_test(false);
  run_test(true);
}
