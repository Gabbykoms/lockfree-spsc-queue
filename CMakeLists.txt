cmake_minimum_required(VERSION 3.16)
project(spsc_queue LANGUAGES CXX)

# --- Standard: C++20, no compiler extensions ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Threads (pulls in pthread on Linux)
find_package(Threads REQUIRED)

# Warnings on by default. You WANT these loud for lock-free work.
add_compile_options(-Wall -Wextra -Wpedantic)

# --- Option: build with ThreadSanitizer ---
# Turn on with:  cmake -DENABLE_TSAN=ON ..
# TSan catches data races your tests would otherwise miss. This is how you
# PROVE ordering is correct, not just "it ran fine".
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer ENABLED")
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
endif()

# --- The sanity-check binary (proves the toolchain works) ---
add_executable(hello_threads src/hello_threads.cpp)
target_link_libraries(hello_threads PRIVATE Threads::Threads)
target_include_directories(hello_threads PRIVATE include)

# --- Stage 1: correctness test ---
add_executable(queue_test tests/queue_test.cpp)
target_link_libraries(queue_test PRIVATE Threads::Threads)
target_include_directories(queue_test PRIVATE include)
# Optimize the test: -O2 makes the compiler MORE aggressive about reordering,
# which is exactly the pressure that exposes broken memory ordering.
target_compile_options(queue_test PRIVATE -O2)

# --- Stage 3: throughput benchmark ---
add_executable(bench_throughput bench/bench_throughput.cpp)
target_link_libraries(bench_throughput PRIVATE Threads::Threads)
target_include_directories(bench_throughput PRIVATE include)
# -O3: maximum optimization. More aggressive reordering = more pressure on the
# memory model, and faster code that makes the false-sharing penalty more visible.
target_compile_options(bench_throughput PRIVATE -O3)

# --- Stage 5: latency distribution benchmark ---
add_executable(bench_latency bench/bench_latency.cpp)
target_link_libraries(bench_latency PRIVATE Threads::Threads)
target_include_directories(bench_latency PRIVATE include)
target_compile_options(bench_latency PRIVATE -O3)

# --- Stage 6: throughput vs. ring capacity ---
add_executable(bench_ring_size bench/bench_ring_size.cpp)
target_link_libraries(bench_ring_size PRIVATE Threads::Threads)
target_include_directories(bench_ring_size PRIVATE include)
target_compile_options(bench_ring_size PRIVATE -O3)

# --- Stage 7: backpressure strategies (spin vs. yield vs. sleep) ---
add_executable(bench_backpressure bench/bench_backpressure.cpp)
target_link_libraries(bench_backpressure PRIVATE Threads::Threads)
target_include_directories(bench_backpressure PRIVATE include)
target_compile_options(bench_backpressure PRIVATE -O3)

# --- Stage 8: batch push/pop amortisation ---
add_executable(bench_batch bench/bench_batch.cpp)
target_link_libraries(bench_batch PRIVATE Threads::Threads)
target_include_directories(bench_batch PRIVATE include)
target_compile_options(bench_batch PRIVATE -O3)