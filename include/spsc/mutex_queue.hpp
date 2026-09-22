#ifndef MUTEX_QUEUE_HPP
#define MUTEX_QUEUE_HPP

#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace spsc {

// Same ring-buffer layout as SpscQueue, but every push/pop is serialized by a
// mutex. Producer and consumer can never overlap — even though they access
// different slots — because the mutex forces them to take turns.
// That serialization is what creates the tail-latency cliff we're measuring.
template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity)
        : buffer_(capacity + 1), capacity_(capacity + 1) {}

    MutexQueue(const MutexQueue&) = delete;
    MutexQueue& operator=(const MutexQueue&) = delete;

    // Blocking mutex: producer and consumer contend on the same lock even though
    // they touch different slots. When one holds it, the other goes to kernel
    // wait — that OS round-trip is the tail-latency source we're measuring.
    bool try_push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t next = (head_ + 1) % capacity_;
        if (next == tail_) return false;  // full

        buffer_[head_] = item;
        head_ = next;
        return true;
    }

    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tail_ == head_) return std::nullopt;  // empty

        T item = buffer_[tail_];
        tail_ = (tail_ + 1) % capacity_;
        return item;
    }

private:
    std::vector<T> buffer_;
    std::size_t    capacity_;
    std::size_t    head_{0};
    std::size_t    tail_{0};
    std::mutex     mutex_;
};

}  // namespace spsc

#endif  // MUTEX_QUEUE_HPP
