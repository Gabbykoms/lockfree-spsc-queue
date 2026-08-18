# cmake_minimum_required(VERSION 3.20)
# project(lockfree-spsc-queue)

# set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# find_package(Threads REQUIRED)

# add_executable(hello_threads src/hello_threads.cpp)
# target_link_libraries(hello_threads PRIVATE Threads::Threads)


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