/*
File : spsc_queue.hpp
Purpose: A correct single-producer/ single-consumer lock free ring buffer


    THE CORE PROBLEM (why atomics + ordering, not a plain array)
    The producer does two things:
        1. Write the item into a slot: buffer_[head] = item
        2. publish it by advancing head: head_ = head +1 


    The consumer does the mirror:
        1. Observe head to see if data is ready
        2. read the item out of the slot

    
    
    Trap: the compiler and CPU are free to reorder 
    the independent writes. if the "index advance" (step 2) becomes visible 
    to the consumer BEFORE the "item write" (step 1), the consumer sees 
    "there's a new item" and reads a slot that hasn't actually been written yet -- garbage.
    The acquire/release exists to prevent this


    acquire/release gives us a one-way barrier:
        -producer's release store on head_ => everything the prodcuer wrote
        BEFORE that store is guaranteed visible to any thread that does an 
        acquire load of head_ and sees the new value
        - consumer's acquire load of head_ => once it sees the new index, it
        is guuaranteed to also see the new item write happened before

    This is cheaper than seq_cst because it only order two operations that matter,
    not everything globally

*/
# ifndef SPSC_QUEUE_HPP
# define SPSC_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>


namespace spsc {

    template <typename T>
    class SpscQueue{
        public:
            //capacity is the number of usable slots
            //we allocate capacity + 1 because this ring uses the classic
            //"one empty slot" trick to distinguish full from empty: empty is head==tail
            //Without the spare slot those two states would look identical
            explicit SpscQueue(std::size_t capacity)
                : buffer_(capacity + 1), capacity_(capacity + 1){}


            //Non-copyable, non-movable: two threads hold a stable reference to one queue. Copying would
            //correctness disaster.
            SpscQueue(const SpscQueue&) = delete;
            SpscQueue& operator=(const SpscQueue&) = delete;

            //called ONLY by the producer thread
            //returns FALSE if the queue is full (item not queued).
            bool try_push(const T& item){
                const std::size_t head = head_.load(std::memory_order_relaxed);
                const std::size_t next = increment(head);

                if (next == tail_.load(std::memory_order_acquire)){
                    return false;       //full
                }

                //write the item
                buffer_[head] = item;

                head_.store(next, std::memory_order_release);
                return true;
            }

            //ONLY called by the consumer thread
            std::optional<T> try_pop(){

                const std::size_t tail = tail_.load(std::memory_order_relaxed);

                if (tail == head_.load(std::memory_order_acquire)){
                    return std::nullopt;        //empty
                }

                T item = buffer_[tail];
                tail_.store(increment(tail), std::memory_order_release);
                return item;
            }

            private:
                std::size_t increment(std::size_t i) const{
                    return (i+1)%capacity_;
                }

                std::vector<T> buffer_;
                std::size_t capacity_;

                //NOTE: (stage 3) head_ and tail_ are declared adjacent here.
                //On most hardware, they'll land on the same 64-byte cache line -> false sharing
                //between producer and consumer. We LEAVE it this way on purpose for now
                // so stage can measure the penalty and then fix it with alignment
                std::atomic<std::size_t> head_{0};      //written by producer, read by consumer
                std::atomic<std::size_t> tail_{0};      //written by consumer, read by producer
            };
}           //namespace spsc

        #endif      //SPSC_QUEUE_HPP



