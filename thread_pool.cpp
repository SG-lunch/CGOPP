#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/task.hpp>
#include <cppcoro/sync_wait.hpp>
#include <cppcoro/when_all.hpp>
#include <cppcoro/schedule_on.hpp>

#include <iostream>
//#include "async_channel.hpp"
#include "async_channel_both_data_dog.hpp"
#include <vector>


/*
 * test/static_thread_pool_tests.cpp
 */


int main() {
//	cppcoro::static_thread_pool thread_pool{2};
	cppcoro::static_thread_pool thread_pool;
	AsyncChannel<int> chan(10, &thread_pool);	
	std::mutex display_mutex;

	auto makeTask = [&]() -> cppcoro::task<> {
		std::unique_lock<std::mutex> lck(display_mutex);
		//lck.lock();
		std::cout << "[INFO] one task\n";
		//lck.unlock();

		co_await thread_pool.schedule();

		//lck.lock();
		std::cout << "[INFO] one task done\n";
		//lck.unlock();
	};

	auto MakeProducer = [&](int id) -> cppcoro::task<> {
		std::unique_lock<std::mutex> lck(display_mutex);
		std::cout << "[INFO] producer " << id << " \n";
		lck.unlock();

		co_await thread_pool.schedule();
		co_await chan.send(id);		
	//	co_await chan.recv_wait_queue_.GetDataDog(id);
		
		lck.lock();
		std::cout << "[INFO] producer " << id << " done\n";
		lck.unlock();
	};

	auto MakeConsumer = [&](int id) -> cppcoro::task<> {
		std::unique_lock<std::mutex> lck(display_mutex);
		std::cout << "[INFO] consumer " << id << " \n";
		lck.unlock();

		co_await thread_pool.schedule();
		int val = co_await chan.recv(); 
	//	int val = co_await chan.send_wait_queue_.GetDataDog();

		lck.lock();
		std::cout << "[INFO] consumer " << id << " get data " << val << " done\n";
		lck.unlock();
	};

	std::vector<cppcoro::task<>> tasks;
	for (int i = 0; i < 100; ++i) {
		//tasks.push_back(makeTask());
		//tasks.push_back(MakeProducer(i));
		//tasks.push_back(MakeConsumer(i));
		
		tasks.push_back(MakeConsumer(i));
		tasks.push_back(MakeProducer(i));
	}

/*	
	for (int i = 0; i < 100; ++i) {
		//tasks.push_back(makeTask());
		//tasks.push_back(MakeProducer(i));
		tasks.push_back(MakeConsumer(i));
	}
	*/
	

	cppcoro::sync_wait(cppcoro::when_all(std::move(tasks)));

	return 0;
}
