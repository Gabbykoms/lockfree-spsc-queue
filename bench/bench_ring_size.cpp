// bench/bench_ring_size.cpp
// Stage 6: throughput vs. ring capacity — exposing cache-hierarchy effects.
//
// The queue holds uint64_t (8 bytes per slot). At each capacity the live
// buffer is capacity * 8 bytes. Cache sizes on Apple M3:
//   L1-D  ~64 KB  ->  fits up to  ~8 192 slots
//   L2    ~4  MB  ->  fits up to ~524 288 slots
//   above that   ->  L3 / DRAM
//
// We expect throughput to drop (possibly with visible kinks) as the buffer
// spills from one cache level to the next.

#include "spsc/spsc_queue.hpp"
#include "bench_utils.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>

static double bench_one(std::size_t capacity, uint64_t N) {
    spsc::SpscQueue<uint64_t> q(capacity);

    std::atomic<bool> go{false};

    std::thread producer([&]() {
        while (!go.load(std::memory_order_acquire)) {}
        for (uint64_t i = 0; i < N; ++i)
            while (!q.try_push(i)) {}
    });

    std::thread consumer([&]() {
        while (!go.load(std::memory_order_acquire)) {}
        uint64_t received = 0;
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
    return static_cast<double>(N) / elapsed / 1e6;  // Mops/s
}

int main() {
    constexpr uint64_t N = 20'000'000;

    // Capacities: powers of two from 2^6 (64) to 2^20 (1 048 576).
    // Buffer bytes = capacity * sizeof(uint64_t).
    constexpr std::size_t caps[] = {
        64, 128, 256, 512, 1024, 2048, 4096, 8192,
        16384, 32768, 65536, 131072, 262144, 524288, 1048576
    };

    std::cout << "items per run : " << N << "    (3 runs each, median reported)\n\n";
    std::cout << std::setw(12) << "capacity"
              << std::setw(14) << "buffer bytes"
              << std::setw(13) << "Mops/s"
              << std::setw(10) << "±stddev"
              << "  note\n"
              << std::string(65, '-') << "\n";

    for (std::size_t cap : caps) {
        std::size_t bytes = cap * sizeof(uint64_t);

        const char* note = "";
        if (cap ==  32768) note = "<- L1 cliff (~256 KB)";
        if (cap == 524288) note = "<- L2 boundary (~4 MB)";

        auto s = run_stats([&]{ return bench_one(cap, N); }, 3);

        std::cout << std::setw(12) << cap
                  << std::setw(14) << bytes
                  << std::setw(13) << std::fixed << std::setprecision(1) << s.median
                  << std::setw(10) << std::setprecision(1) << s.stddev
                  << "  " << note << "\n";
    }

    return 0;
}
