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

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

int main() {
    constexpr std::uint64_t N        = 10'000'000;  // items to transfer
    constexpr std::size_t   CAPACITY = 4096;         // ring size

    spsc::SpscQueue<std::uint64_t> q(CAPACITY);

    // --- start timing AFTER both threads are created, so thread-spawn cost
    //     doesn't skew the result ---
    std::atomic<bool> go{false};

    std::thread producer([&]() {
        while (!go.load(std::memory_order_acquire)) {}  // wait for start gun
        for (std::uint64_t i = 0; i < N; ++i) {
            while (!q.try_push(i)) {}  // spin until slot free
        }
    });

    std::thread consumer([&]() {
        while (!go.load(std::memory_order_acquire)) {}  // wait for start gun
        std::uint64_t received = 0;
        while (received < N) {
            if (q.try_pop()) ++received;
        }
    });

    // fire both threads at once so we're measuring steady-state contention,
    // not one thread racing ahead of the other
    auto t0 = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);

    producer.join();
    consumer.join();
    auto t1 = std::chrono::steady_clock::now();

    double elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    double mops      = static_cast<double>(N) / elapsed_s / 1e6;

    std::cout << "transferred : " << N << " items\n"
              << "elapsed     : " << elapsed_s << " s\n"
              << "throughput  : " << mops << " Mops/s\n";

    return 0;
}