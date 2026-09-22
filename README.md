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

# Latency distribution benchmark (lock-free vs mutex, p50/p99/p999)
cmake --build build-bench --target bench_latency
./build-bench/bench_latency

# Throughput vs. ring capacity sweep (exposes cache-hierarchy effects)
cmake --build build-bench --target bench_ring_size
./build-bench/bench_ring_size
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

### Stage 5 — Latency distribution vs. mutex baseline 
See findings below.

### Stage 6 — Throughput vs. ring capacity 
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

## Stage 5: Latency Distribution — Lock-Free vs Mutex

### Setup

`bench/bench_latency.cpp` measures **one-way latency** per item: the producer
stamps each item with `steady_clock::now()` before pushing; the consumer
records `now() - stamp` after popping. In-flight depth is capped at 64 items
so latency reflects transmission time, not queue backpressure.

The mutex queue (`include/spsc/mutex_queue.hpp`) uses the same ring-buffer
layout as the lock-free queue but protects every push/pop with a blocking
`std::mutex`. Both threads contend on the same lock even though they never
touch the same slot — that forced serialisation is the source of tail latency.

### Results (Apple M3, 1M items, ring capacity 4096)

| Queue | p50 ns | p99 ns | p999 ns | max ns |
|---|---|---|---|---|
| **lock-free** | 4,792 | 7,958 | 16,541 | 214,417 |
| **mutex** | 2,542 | 37,667 | 73,750 | 159,875 |

### Why

**Median (p50):** The mutex queue is surprisingly faster at the median on
Apple Silicon. Apple's lock implementation is highly optimised — when there
is no contention the fast path is a single atomic compare-and-swap without a
syscall. The lock-free queue spins checking `head_`, which can suffer a brief
cache-line round-trip before the consumer sees the new value.

**Tail (p99 / p999):** This is where lock-free wins decisively. The mutex
p99 is **4.7× higher** than lock-free (38 µs vs 8 µs); p999 is **4.5×
higher** (74 µs vs 17 µs). When both threads arrive at the mutex at the same
time, one blocks — a kernel wait that costs microseconds, not nanoseconds.
Lock-free threads never enter the kernel; they spin in userspace and the wait
is bounded by the time for a cache-line to travel between cores (~50–200 ns
on M3).

**Conclusion:** Median latency alone does not tell the full story. The mutex
is competitive — even faster — at p50. The argument for lock-free is in the
*tail*: predictable, sub-microsecond p99 vs multi-microsecond spikes under
the mutex. For latency-sensitive pipelines the tail is what pages the on-call
engineer.

---

## Stage 6: Throughput vs. Ring Capacity

### Setup

`bench/bench_ring_size.cpp` sweeps ring capacity from 64 to 1,048,576 slots
(powers of two). Each slot holds a `uint64_t` (8 bytes), so the live buffer
ranges from 512 B to 8 MB. Every point transfers 20 M items to keep timing
stable. The producer and consumer spin as before.

### Results (Apple M3, 20M items per run)

| Capacity | Buffer | Mops/s | Region |
|---|---|---|---|
| 64 | 512 B | 114.2 | L1 |
| 128 | 1 KB | 125.8 | L1 |
| 256 | 2 KB | 127.1 | L1 |
| 512 | 4 KB | 114.0 | L1 |
| 1 024 | 8 KB | 102.6 | L1 |
| 2 048 | 16 KB | 124.1 | L1 |
| 4 096 | 32 KB | 114.8 | L1 |
| 8 192 | 64 KB | 113.1 | L1 |
| 16 384 | 128 KB | 100.9 | L1→L2 transition |
| **32 768** | **256 KB** | **58.5** | **← cliff** |
| 65 536 | 512 KB | 53.1 | L2 |
| 131 072 | 1 MB | 52.3 | L2 |
| 262 144 | 2 MB | 54.7 | L2 |
| 524 288 | 4 MB | 45.5 | L2→L3 |
| 1 048 576 | 8 MB | 40.6 | L3 |

### Why

**The L1 plateau (64–8192 slots, ~100–128 Mops/s):** The entire buffer fits
in each core's L1 data cache. Cache lines travel between the two cores on the
on-chip interconnect; every access is a cache-line transfer, not a memory
fetch. Throughput is flat because the bottleneck is the atomic acquire/release,
not the memory system.

**The cliff at 32768 slots (256 KB → 58.5 Mops/s, ~50% drop):** The M3's
L1-D is 128 KB per P-core — not 64 KB as the architecture label sometimes
implies. Once the buffer exceeds 128 KB it no longer fits in either core's
L1. Both producer and consumer start taking L2 hits on the slots they access,
roughly halving throughput.

**The L2 plateau (32768–262144 slots, ~52–55 Mops/s):** Accesses land in the
shared L2 (~16 MB on M3). Latency is higher than L1 but consistent, so
throughput is stable across this range.

**The second drop at 524288 slots (4 MB, ~45 Mops/s):** The buffer is now
approaching the L2 capacity. Evictions into L3 add another latency tier,
pushing throughput down a further ~10–15%.

**Conclusion:** Ring capacity is not a free parameter. Sizing the queue to
fit in L1 (~8K slots for `uint64_t` on M3) gives roughly 3× the throughput
of an 8 MB queue. The right size depends on the item type and target hardware
— measure, don't guess.

---

## Next Steps

- [ ] Hardware counter profiling (cache-miss events, not just throughput)
- [ ] Backpressure strategies: spin vs. yield vs. sleep — CPU cost vs. latency tradeoff
- [ ] Read: "Is Parallel Programming Hard?" (Paul McKenney) — the formal
      memory model treatment