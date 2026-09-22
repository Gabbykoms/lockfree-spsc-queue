// bench/bench_throughput.cpp
// Stage 3: measure the throughput cost of false sharing between head_ and tail_.
//
// How it works:
//   - Producer pushes N items as fast as possible (spin on full)
//   - Consumer pops N items as fast as possible (spin on empty)
//   - We time the whole round trip and compute million items / second
//
// Run this BEFORE the alignas fix, record the number.
// Apply the fix to spsc_queue.hpp, rebuild, run again, record the number.
// The delta is your war story.

#include "spsc/spsc_queue.hpp"
#include "bench_utils.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>

static double one_run(std::uint64_t N, std::size_t capacity) {
    spsc::SpscQueue<std::uint64_t> q(capacity);
    std::atomic<bool> go{false};

    std::thread producer([&]() {
        while (!go.load(std::memory_order_acquire)) {}
        for (std::uint64_t i = 0; i < N; ++i)
            while (!q.try_push(i)) {}
    });

    std::thread consumer([&]() {
        while (!go.load(std::memory_order_acquire)) {}
        std::uint64_t received = 0;
        while (received < N) {
            if (q.try_pop()) ++received;
        }
    });

    auto t0 = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    auto t1 = std::chrono::steady_clock::now();

    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    return static_cast<double>(N) / elapsed / 1e6;
}

int main() {
    constexpr std::uint64_t  N        = 10'000'000;
    constexpr std::size_t    CAPACITY = 4096;

    std::cout << "transferred : " << N << " items    ring : " << CAPACITY << "\n"
              << "(5 runs)\n\n";

    auto s = run_stats([&]{ return one_run(N, CAPACITY); }, 5);

    std::cout << std::fixed << std::setprecision(1)
              << "  mean   : " << s.mean   << " Mops/s\n"
              << "  median : " << s.median << " Mops/s\n"
              << "  stddev : " << s.stddev << " Mops/s\n";
    return 0;
}
