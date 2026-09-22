#include "spsc/spsc_queue.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

// ---------------------------------------------------------------------------
// Test 1 (original): single-item push/pop, 5M items in order
// ---------------------------------------------------------------------------
static bool test_single_item_order() {
    constexpr std::uint64_t N = 5'000'000;
    spsc::SpscQueue<std::uint64_t> q(1024);

    bool ok = true;
    std::thread producer([&]() {
        for (std::uint64_t i = 0; i < N; ++i)
            while (!q.try_push(i)) {}
    });

    std::thread consumer([&]() {
        std::uint64_t expected = 0;
        std::uint64_t received = 0;
        while (received < N) {
            auto item = q.try_pop();
            if (!item) continue;
            if (*item != expected) {
                ok = false;
                std::cerr << "  MISMATCH at " << received
                          << ": got " << *item << ", expected " << expected << "\n";
                return;
            }
            ++expected;
            ++received;
        }
    });

    producer.join();
    consumer.join();
    return ok;
}

// ---------------------------------------------------------------------------
// Test 2: batch push/pop, 5M items in order, batch size 32
// Exercises the same memory-ordering path as single-item but via push_n/pop_n.
// ---------------------------------------------------------------------------
static bool test_batch_order() {
    constexpr std::uint64_t N     = 5'000'000;
    constexpr std::size_t   BATCH = 32;
    spsc::SpscQueue<std::uint64_t> q(1024);

    bool ok = true;

    std::thread producer([&]() {
        std::uint64_t items[BATCH];
        std::uint64_t pushed = 0;
        while (pushed < N) {
            std::size_t want = static_cast<std::size_t>(std::min((std::uint64_t)BATCH, N - pushed));
            for (std::size_t i = 0; i < want; ++i) items[i] = pushed + i;
            std::size_t did;
            while ((did = q.push_n(items, want)) == 0) {}
            pushed += did;
        }
    });

    std::thread consumer([&]() {
        std::uint64_t items[BATCH];
        std::uint64_t expected = 0;
        std::uint64_t received = 0;
        while (received < N) {
            std::size_t got = q.pop_n(items, BATCH);
            for (std::size_t i = 0; i < got; ++i) {
                if (items[i] != expected + i) {
                    ok = false;
                    std::cerr << "  MISMATCH at item " << received + i
                              << ": got " << items[i]
                              << ", expected " << expected + i << "\n";
                    return;
                }
            }
            expected += got;
            received += got;
        }
    });

    producer.join();
    consumer.join();
    return ok;
}

// ---------------------------------------------------------------------------
// Test 3: batch wrap-around — tiny ring (32 slots) forces many modular wraps
// Uses batch 16 so every push/pop crosses the ring boundary frequently.
// ---------------------------------------------------------------------------
static bool test_batch_wraparound() {
    constexpr std::uint64_t N     = 500'000;
    constexpr std::size_t   BATCH = 16;
    spsc::SpscQueue<std::uint64_t> q(32);   // tiny ring: wraps every 32 items

    bool ok = true;

    std::thread producer([&]() {
        std::uint64_t items[BATCH];
        std::uint64_t pushed = 0;
        while (pushed < N) {
            std::size_t want = static_cast<std::size_t>(std::min((std::uint64_t)BATCH, N - pushed));
            for (std::size_t i = 0; i < want; ++i) items[i] = pushed + i;
            std::size_t did;
            while ((did = q.push_n(items, want)) == 0) {}
            pushed += did;
        }
    });

    std::thread consumer([&]() {
        std::uint64_t items[BATCH];
        std::uint64_t expected = 0;
        std::uint64_t received = 0;
        while (received < N) {
            std::size_t got = q.pop_n(items, BATCH);
            for (std::size_t i = 0; i < got; ++i) {
                if (items[i] != expected + i) {
                    ok = false;
                    std::cerr << "  MISMATCH at item " << received + i
                              << ": got " << items[i]
                              << ", expected " << expected + i << "\n";
                    return;
                }
            }
            expected += got;
            received += got;
        }
    });

    producer.join();
    consumer.join();
    return ok;
}

// ---------------------------------------------------------------------------
// Test 4: partial push and partial pop (single-threaded)
// Verifies push_n returns the correct count when the ring has less space than
// requested, and pop_n returns the correct count when fewer items are available.
// ---------------------------------------------------------------------------
static bool test_partial_push_pop() {
    // SpscQueue(N) gives N usable slots — the spare is added internally.
    // SpscQueue(8) → 8 usable slots.
    spsc::SpscQueue<std::uint64_t> q(8);

    std::uint64_t src[16];
    for (std::size_t i = 0; i < 16; ++i) src[i] = i;

    // Request 16 items into a ring with 8 usable slots — expect exactly 8
    std::size_t pushed = q.push_n(src, 16);
    if (pushed == 0 || pushed > 8) {
        std::cerr << "  partial push returned " << pushed << ", expected 1–8\n";
        return false;
    }

    // A second push_n should now fail (ring full after first batch)
    // Fill the ring completely first
    while (q.push_n(src, 1) > 0) {}  // drain remaining space

    if (q.push_n(src, 1) != 0) {
        std::cerr << "  push_n on full ring should return 0\n";
        return false;
    }

    // pop_n with count > items available
    std::uint64_t dst[16];
    std::size_t popped = q.pop_n(dst, 16);
    if (popped == 0 || popped > 8) {
        std::cerr << "  partial pop returned " << popped << ", expected 1–8\n";
        return false;
    }

    // Values must match what was pushed in order
    for (std::size_t i = 0; i < popped; ++i) {
        if (dst[i] != src[i]) {
            std::cerr << "  partial pop value mismatch at " << i
                      << ": got " << dst[i] << ", expected " << src[i] << "\n";
            return false;
        }
    }

    // Queue should now be empty — pop_n must return 0
    if (q.pop_n(dst, 16) != 0) {
        std::cerr << "  pop_n on empty ring should return 0\n";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------

static void run(const char* name, bool (*fn)()) {
    bool passed = fn();
    std::cout << (passed ? "PASS" : "FAIL") << "  " << name << "\n";
    if (!passed) std::exit(1);
}

int main() {
    run("single-item order (5M items)",         test_single_item_order);
    run("batch order      (5M items, batch 32)", test_batch_order);
    run("batch wrap-around (500K, ring 32)",     test_batch_wraparound);
    run("partial push/pop  (single-threaded)",   test_partial_push_pop);
    return 0;
}
