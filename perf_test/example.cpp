#include <iostream>
#include <vector>
#include <chrono>

#include "ThreadPool.h"

void example1(){
	ThreadPool pool(4);
    std::vector< std::future<int> > results;

    for(int i = 0; i < 8; ++i) {
        results.emplace_back(
            pool.enqueue([i] {
                std::cout << "hello " << i << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::cout << "world " << i << std::endl;
                return i*i;
            })
        );
    }

    for(auto && result: results)
        std::cout << result.get() << ' ';
    std::cout << std::endl;
}

void example2_worker(int i){
	int a;
	std::cout << "Input an integer to " << i << ": ";
	std::cin >> a;
	std::cout << "Input to " << i << " is: " << a << std::endl;
}

void example2(){
	ThreadPool pool(4);
	for(int i = 0; i < 8; ++i) {
        pool.enqueue(example2_worker, i);
    }
}

int main()
{
    //example1();
	example2();
    return 0;
}
