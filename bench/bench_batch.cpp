// bench/bench_batch.cpp
// Stage 8: batch push/pop — amortising atomic operations over multiple items.
//
// try_push / try_pop cost one acquire load + one release store per item.
// push_n  / pop_n  cost the same atomics but spread over a whole batch, so
// per-item atomic overhead shrinks as batch size grows.
//
// What we measure: throughput (Mops/s) at batch sizes 1, 2, 4, 8, 16, 32, 64.
// Ring is sized in the L1 plateau (4096 slots) so memory is not the bottleneck
// and the atomic amortisation effect is visible in isolation.

#include "spsc/spsc_queue.hpp"
#include "bench_utils.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>

static double bench(std::size_t capacity, uint64_t N, std::size_t batch) {
    spsc::SpscQueue<uint64_t> q(capacity);

    std::atomic<bool> go{false};

    std::thread producer([&]() {
        while (!go.load(std::memory_order_acquire)) {}

        // stack-local batch buffer; batch <= 64 so this is always small
        uint64_t buf[64];

        if (batch == 1) {
            for (uint64_t i = 0; i < N; ++i)
                while (!q.try_push(i)) {}
        } else {
            uint64_t pushed = 0;
            while (pushed < N) {
                std::size_t want = std::min((uint64_t)batch, N - pushed);
                for (std::size_t i = 0; i < want; ++i) buf[i] = pushed + i;
                std::size_t did;
                while ((did = q.push_n(buf, want)) == 0) {}
                pushed += did;
            }
        }
    });

    std::thread consumer([&]() {
        while (!go.load(std::memory_order_acquire)) {}

        uint64_t buf[64];

        if (batch == 1) {
            uint64_t received = 0;
            while (received < N) {
                if (q.try_pop()) ++received;
            }
        } else {
            uint64_t received = 0;
            while (received < N) {
                std::size_t got = q.pop_n(buf, batch);
                received += got;
            }
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
    constexpr uint64_t    N        = 20'000'000;
    constexpr std::size_t CAPACITY = 4096;

    std::cout << "items : " << N << "    ring capacity : " << CAPACITY << "\n\n";
    std::cout << std::setw(10) << "batch"
              << std::setw(12) << "Mops/s"
              << std::setw(10) << "±stddev"
              << std::setw(16) << "vs single-item"
              << "\n"
              << std::string(48, '-') << "\n";

    const std::size_t batches[] = {1, 2, 4, 8, 16, 32, 64};
    double base = 0.0;

    for (std::size_t b : batches) {
        auto s = run_stats([&]{ return bench(CAPACITY, N, b); }, 5);
        if (b == 1) base = s.median;
        char ratio[16];
        std::snprintf(ratio, sizeof(ratio), "%.2fx", s.median / base);
        std::cout << std::setw(10) << b
                  << std::setw(12) << std::fixed << std::setprecision(1) << s.median
                  << std::setw(10) << std::setprecision(1) << s.stddev
                  << std::setw(16) << ratio
                  << "\n";
    }

    return 0;
}
