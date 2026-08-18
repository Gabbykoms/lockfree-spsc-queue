# Lock-Free SPSC Queue (C++20)

A single-producer / single-consumer lock-free ring buffer built to develop
hands-on fluency in low-level concurrent C++: the memory model, atomic
ordering guarantees, and cache effects on multicore hardware.

---

## Why SPSC, why lock-free

A mutex-protected queue is simpler, but under contention a blocked thread is
put to sleep by the OS — a kernel round-trip costing microseconds, with an
unpredictable tail. For latency-sensitive work, that jitter is the problem,
not just the average cost.

A lock-free SPSC queue keeps both threads in userspace and gives bounded,
predictable latency. The cost is that the programmer must enforce ordering
correctly using the C++ memory model, rather than relying on a lock to do it.

---

## Implementation

**`include/spsc/spsc_queue.hpp`** — header-only template, fixed-size ring buffer.

Key design choices:

- **One spare slot** to distinguish full from empty: `empty = head == tail`,
  `full = (head + 1) % capacity == tail`. Without the spare slot these two
  states are indistinguishable.
- **Release store on `head_` (producer):** the item write cannot be reordered
  past this store. Any consumer that acquire-loads the new index is guaranteed
  to see the item.
- **Acquire load of `head_` (consumer):** once the new index is visible, the
  item write that happened-before it is also visible. This pairs with the
  producer's release.
- **Relaxed loads of own index:** each thread is the sole writer of its own
  index, so no cross-thread ordering guarantee is needed for that load.

---

## Build

**Prerequisites:** CMake ≥ 3.16, a C++20-capable compiler (g++ 13+ or
Apple Clang 15+).

```bash
# Standard build
cmake -S . -B build
cmake --build build

# Sanity check (threads + atomics)
./build/hello_threads

# Correctness test (5M items, in-order transfer)
./build/queue_test

# ThreadSanitizer build (proves ordering is correct, not just working)
cmake -S . -B build-tsan -DENABLE_TSAN=ON
cmake --build build-tsan
./build-tsan/queue_test

# Throughput benchmark
cmake -S . -B build-bench
cmake --build build-bench
./build-bench/bench_throughput
```

---

## Stages

### Stage 0 — Toolchain 
`hello_threads`: two threads increment a shared `std::atomic<int>` to 2,000,000.
Proves the compiler, linker, and threading library work end-to-end before
touching anything complex.

### Stage 1 — Correctness 
Fixed-size ring buffer with acquire/release ordering. Correctness test pushes
5,000,000 sequential values on a producer thread and verifies exact in-order
arrival on the consumer thread.

### Stage 2 — ThreadSanitizer validation 
The queue passes TSan's happens-before analysis across 5M iterations with zero
reported races. "It ran fine" is not the same as "it is correct" — TSan is how
you prove the latter.

### Stage 3 — False sharing experiment 
See findings below.

---

## Stage 3: False Sharing — Measurement and Findings

### Setup

`head_` (written by producer, read by consumer) and `tail_` (written by
consumer, read by producer) are declared adjacent in the struct. On most
hardware they land on the same 64-byte cache line.

The standard advice for lock-free data structures is to pad each hot atomic
onto its own cache line with `alignas(64)` to prevent false sharing — the
scenario where one thread's write invalidates a cache line the other thread
holds, even though they never touch the same variable.

Hypothesis before measuring: separating the indices should improve throughput
by eliminating that ping-pong.

### Results (Apple M3, 10M items)

| Configuration | Throughput |
|---|---|
| Adjacent (baseline) | **104.2 Mops/s** |
| `alignas(64)` separated | 64–75 Mops/s |

**The fix made it ~28–39% slower.** The hypothesis was wrong.

### Why

The access pattern in an SPSC queue is asymmetric:
- Producer *writes* `head_`, consumer *reads* `head_` (never writes it)
- Consumer *writes* `tail_`, producer *reads* `tail_` (never writes it)

False sharing is expensive when **both threads write** to the same cache line,
causing constant ownership transfers under the MESI/MOESI protocol. That is
not what is happening here. Each variable has exactly one writer. The opposite
thread only reads it.

When both variables share a line, the reader gets both in a single cache
fetch — spatial locality working in our favor. When `alignas(64)` forces them
apart, each thread must now maintain two separate cache lines instead of one,
and the extra fetch cost exceeds the coherence penalty eliminated.

On x86 this tradeoff may land differently — x86's TSO memory model and MESI
invalidation behavior under high contention can make false sharing more
expensive. This experiment was run on Apple Silicon (ARM), where the
on-chip interconnect between cores is faster and the coherence penalty is
lower, so the separation cost dominates.

**Conclusion:** `alignas(64)` padding is the right fix when both threads
write to the same cache line. For SPSC queues where each index has a single
writer, it is likely counterproductive — especially on ARM. Measure before
applying.

The original adjacent layout is retained. To properly attribute cache
coherence events rather than inferring from throughput, the next step would
be hardware performance counters (`perf` on Linux, Instruments on macOS).

---

## Next Steps

- [ ] Hardware counter profiling (cache-miss events, not just throughput)
- [ ] p99 latency distribution vs. mutex-based baseline
- [ ] Stress harness: vary ring size, measure throughput curve vs. capacity
- [ ] Read: "Is Parallel Programming Hard?" (Paul McKenney) — the formal
      memory model treatment