// bench/bench_backpressure.cpp
// Stage 7: backpressure strategies — spin vs. yield vs. sleep.
//
// When the queue is full (producer) or empty (consumer) the thread must wait.
// Three strategies:
//   Spin  — retry immediately in a tight loop. Zero OS involvement, lowest
//            latency, but burns 100% of a CPU core while waiting.
//   Yield — call std::this_thread::yield(). Hints the scheduler to run
//            something else. Saves CPU when the other thread needs time;
//            adds a scheduling round-trip (~1-10 µs) when it doesn't.
//   Sleep — sleep_for(1 µs). Explicit pause. Most CPU-friendly; worst tail
//            latency because the thread may sleep longer than necessary.
//
// What we measure:
//   - Throughput (Mops/s): does the wait strategy limit how fast items move?
//   - p99 latency: does yielding/sleeping add jitter to individual items?
//   - CPU time / wall time ratio: how much of a core does each strategy burn?
//
// A 256-slot ring forces frequent full/empty conditions so the wait path is
// actually exercised, not just the hot path.

#include "spsc/spsc_queue.hpp"
#include "bench_utils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>
#include <sys/resource.h>

static inline uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

static double cpu_ms() {
    struct rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return (ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000.0
         + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000.0;
}

enum class Strategy { Spin, Yield, Sleep };

template <Strategy S>
static void wait_once() {
    if constexpr (S == Strategy::Yield) {
        std::this_thread::yield();
    } else if constexpr (S == Strategy::Sleep) {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    // Spin: no hint — compiler will emit a tight retry loop
}

struct Result {
    double   mops;
    int64_t  p50, p99, p999;
    double   cpu_pct;  // CPU ms / wall ms * 100, summed over both threads
};

// In-flight cap: producer may not be more than this many items ahead of
// consumer. Keeps individual latency meaningful (not dominated by queue depth).
static constexpr uint64_t MAX_INFLIGHT = 32;

template <Strategy S>
static Result bench(std::size_t capacity, uint64_t N) {
    spsc::SpscQueue<uint64_t> q(capacity);

    std::vector<int64_t> latencies;
    latencies.reserve(N);

    std::atomic<bool>     go{false};
    std::atomic<uint64_t> consumed{0};

    double cpu_before = cpu_ms();
    auto   wall_start = std::chrono::steady_clock::now();

    std::thread producer([&]() {
        while (!go.load(std::memory_order_acquire)) {}
        for (uint64_t pushed = 0; pushed < N; ++pushed) {
            while (pushed - consumed.load(std::memory_order_acquire) >= MAX_INFLIGHT)
                wait_once<S>();
            uint64_t t = now_ns();
            while (!q.try_push(t))
                wait_once<S>();
        }
    });

    std::thread consumer([&]() {
        while (!go.load(std::memory_order_acquire)) {}
        uint64_t received = 0;
        while (received < N) {
            auto val = q.try_pop();
            if (val) {
                latencies.push_back(static_cast<int64_t>(now_ns() - *val));
                consumed.fetch_add(1, std::memory_order_release);
                ++received;
            } else {
                wait_once<S>();
            }
        }
    });

    go.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    double wall_ms  = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - wall_start).count();
    double cpu_used = cpu_ms() - cpu_before;

    std::sort(latencies.begin(), latencies.end());

    return {
        static_cast<double>(N) / (wall_ms / 1000.0) / 1e6,
        latencies[N * 50  / 100],
        latencies[N * 99  / 100],
        latencies[N * 999 / 1000],
        // two threads => max possible CPU is 2 * wall_ms
        cpu_used / (2.0 * wall_ms) * 100.0,
    };
}

// Run the full bench 5 times per strategy; report median Mops/s ± stddev.
// Latency percentiles come from the last run (1M samples is already stable).
template <Strategy S>
static void print_row(const char* name, std::size_t capacity, uint64_t N) {
    Result last{};
    auto s = run_stats([&]{
        last = bench<S>(capacity, N);
        return last.mops;
    }, 5);

    std::cout << std::setw(8)  << name
              << std::setw(10) << std::fixed << std::setprecision(1) << s.median
              << std::setw(8)  << std::setprecision(1) << s.stddev
              << std::setw(10) << last.p50
              << std::setw(10) << last.p99
              << std::setw(11) << last.p999
              << std::setw(10) << std::setprecision(1) << last.cpu_pct << "%"
              << "\n";
}

int main() {
    constexpr uint64_t    N        = 1'000'000;
    constexpr std::size_t CAPACITY = 256;

    std::cout << "items : " << N << "    ring capacity : " << CAPACITY
              << "    in-flight cap : " << MAX_INFLIGHT
              << "    (5 runs, median Mops/s)\n\n";

    std::cout << std::setw(8)  << "strategy"
              << std::setw(10) << "Mops/s"
              << std::setw(8)  << "±stddev"
              << std::setw(10) << "p50 ns"
              << std::setw(10) << "p99 ns"
              << std::setw(11) << "p999 ns"
              << std::setw(10) << "CPU use"
              << "\n"
              << std::string(67, '-') << "\n";

    print_row<Strategy::Spin> ("spin",  CAPACITY, N);
    print_row<Strategy::Yield>("yield", CAPACITY, N);
    print_row<Strategy::Sleep>("sleep", CAPACITY, N);

    return 0;
}
