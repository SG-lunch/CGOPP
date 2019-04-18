#ifndef CPPCORO_CHANNEL_HPP_INCLUDED
#define CPPCORO_CHANNEL_HPP_INCLUDED

#include <experimental/coroutine>
#include <mutex>
#include <atomic>
//#include <memory>

//namespace sgwf {


template<typename DataType>
class SyncChannel {
public:
	//delete copy/move
	SyncChannel(const SyncChannel&) = delete;
	SyncChannel(SyncChannel&&) = delete;
	SyncChannel& operator=(const SyncChannel&) = delete;
	SyncChannel& operator=(SyncChannel&&) = delete;



	class awaiter;
	awaiter operator co_await() const noexcept;

	bool has_recv() {
	}

	bool has_send() {
	}
	

private:
	friend struct awaiter;

	//std::atomic<bool> ;

	// "this" means the queue is empty
	// otherwise, refers to the head of the queue;
	mutable std::atomic<void*> send_wait_queue_head_;
	mutable std::atomic<void*> recv_wait_queue_head_; 
};

class SyncChannel::awaiter {
public:
	bool await_ready() const noexcept {
		return 
	}
	bool await_suspend(std::experimental::coroutine_handle<> awaitingCoroutine) noexcept;
	void await_resume() noexcept {
		// get data
	}
n
private:
	const SyncChannel& m_chan;
	std::experimental::coroutine_handle<> m_awaitingCoroutine;
	awaiter* m_next;

};



class AsyncChannel {
	
};
//}


