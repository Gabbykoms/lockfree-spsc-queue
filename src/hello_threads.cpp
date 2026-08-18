/* hello_threads.cpp
  Purpose : prove the toolchain compile, links against the threading library, 
  and actually runsa second thread.*/

#include <atomic>
#include <iostream>
#include <thread>

int main(){

    /* A shared atomic counter, incremented from two threads
        if threading  + atomics link and run, we get 2000000 every time
    */
    std::atomic<int> counter {0};
    
    auto work = [&counter](){
        for(int i = 0; i < 1000000; i++){
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };
    std::thread t1(work);
    std::thread t2(work);
    t1.join();
    t2.join();

    std::cout << "counter = " << counter.load() << "(expected 2000000) \n";
    std::cout << "toolchain OK \n";


    return 0;
}