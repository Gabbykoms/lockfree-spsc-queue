#include "spsc/spsc_queue.hpp"


#include <cstdint>
#include <iostream>
#include <thread>

int main(){
    constexpr std::uint64_t N = 5'000'000;  //items to send
    spsc::SpscQueue<std::uint64_t> q(1024);     //small ring forcing around

    std::thread producer([&q](){
        for (std::uint64_t i = 0; i < N; i++){
            while (!q.try_push(i)){
                //busy-wait: consumer will drain and free a slot
            }
        }
    });

    // consumer: pop N items, check each equals to the value we expect next.
    bool ok = true;
    std::uint64_t expected = 0;
    std::thread consumer([&q, &ok, &expected](){
        std::uint64_t received = 0;
        while (received < N){
            auto item = q.try_pop();
            if(!item) continue;
            if (*item != expected){
                ok = false;
                std::cerr << "MISMATCH at " << received
                          << ": got " << *item
                          << ", expected " << expected << "\n";
                return;

            }
            ++expected;
            ++received;
        }
    });

    producer.join();
    consumer.join();

    if (ok && expected == N){
        std::cout << "PASS: " << N << " items transferred in order \n";
        return 0;
    }

    std::cout << "FAIL \n";
    return 1;


    

}