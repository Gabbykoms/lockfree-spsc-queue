// bench/bench_latency.cpp
// Stage 5: per-item one-way latency distribution — lock-free SPSC vs mutex queue.
//
// How it works:
//   - Producer pushes the current timestamp (ns) as the item value.
//   - Consumer pops it, records (now - timestamp) as one-way latency.
//   - After N items, sort the latency vector and print p50 / p99 / p999 / max.
//
// The clock (steady_clock) is shared across cores on modern hardware, so
// subtracting two timestamps from different threads gives a valid interval.

#include "spsc/mutex_queue.hpp"
#include "spsc/spsc_queue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

static inline uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

struct Stats {
    int64_t p50, p99, p999, max;
};

// Throttle in-flight items to MAX_INFLIGHT so latency reflects transmission
// time, not queue backpressure. Producer may only be MAX_INFLIGHT items ahead
// of the consumer at any moment. With MAX_INFLIGHT=1 each item must be popped
// before the next is pushed — cleanest latency signal, but synchronisation
// overhead dominates. A small window (e.g. 64) keeps the queues busy while
// still giving meaningful per-item latency numbers.
static constexpr uint64_t MAX_INFLIGHT = 64;

template <typename Queue>
Stats measure(Queue& q, uint64_t N) {
    std::vector<int64_t> latencies;
    latencies.reserve(N);

    std::atomic<bool>     go{false};
    std::atomic<uint64_t> consumed{0};  // how many items the consumer has popped

    std::thread producer([&]() {
        while (!go.load(std::memory_order_acquire)) {}
        for (uint64_t pushed = 0; pushed < N; ++pushed) {
            // back-pressure: don't get more than MAX_INFLIGHT ahead
            while (pushed - consumed.load(std::memory_order_acquire) >= MAX_INFLIGHT) {}
            uint64_t t = now_ns();
            while (!q.try_push(t)) {}
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
            }
        }
    });

    go.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    std::sort(latencies.begin(), latencies.end());

    return {
        latencies[N * 50  / 100],
        latencies[N * 99  / 100],
        latencies[N * 999 / 1000],
        latencies.back(),
    };
}

int main() {
    constexpr uint64_t     N        = 1'000'000;
    constexpr std::size_t  CAPACITY = 4096;

    std::cout << "items : " << N << "    ring capacity : " << CAPACITY << "\n\n";
    std::cout << std::setw(12) << "queue"
              << std::setw(10) << "p50 ns"
              << std::setw(10) << "p99 ns"
              << std::setw(11) << "p999 ns"
              << std::setw(10) << "max ns"
              << "\n"
              << std::string(53, '-') << "\n";

    {
        spsc::SpscQueue<uint64_t> q(CAPACITY);
        auto s = measure(q, N);
        std::cout << std::setw(12) << "lock-free"
                  << std::setw(10) << s.p50
                  << std::setw(10) << s.p99
                  << std::setw(11) << s.p999
                  << std::setw(10) << s.max
                  << "\n";
    }

    {
        spsc::MutexQueue<uint64_t> q(CAPACITY);
        auto s = measure(q, N);
        std::cout << std::setw(12) << "mutex"
                  << std::setw(10) << s.p50
                  << std::setw(10) << s.p99
                  << std::setw(11) << s.p999
                  << std::setw(10) << s.max
                  << "\n";
    }

    return 0;
}
