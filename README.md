## Lock Free SPSC Queue (C++ 20)

A single-producer/single-consumer lock free ring buffer, built just to refresh my fundamentals of getting closer to the metal

### why SPSC, why lock-free
A mutex protected queue is simpler, but under contention, a blocked thread can be put to sleep by the OS. a kernel round trip costing microseconds with an unpredictable tail. For latency sensitive work, that delay is a problem. A lock-free SPSC queue keeps both threads in userspace and give bounded, predictable latency.
The cost is that I have to enforce ordering correctly using C++ memory model, instead of leaning on a lock to do it for me

### Build


cmake -S . -B build 

cmake --build build

./build/hello_threads

#
cmake -S . -B build && cmake --build build 
 ./build/hello_threads
 ./build/queue_test

