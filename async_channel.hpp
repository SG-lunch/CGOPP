#ifndef CPPCORO_CHANNEL_HPP_INCLUDED
#define CPPCORO_CHANNEL_HPP_INCLUDED

#include <iostream>
#include <experimental/coroutine>
#include <mutex>
#include <atomic>
#include <vector>
#include <list>


template<typename T>
class QueueItem {
public:
	std::unique_ptr<T> data;
	//cppcoro::static_thread_pool* thread_pool;
	std::experimental::coroutine_handle<> awaiter;
	
	QueueItem(std::experimental::coroutine_handle<> _awaiter,
				const T& _data) : awaiter(_awaiter) {
		// for send queue
		data = make_unique<T>(_data);
	}
	
	QueueItem(std::experimental::coroutine_handle<> _awaiter) :  
				awaiter(_awaiter) {
		// for recv queue
		data = make_unique<T>();
	}

//	QueueItem(cppcoro::static_thread_pool* _thread_pool, 
//				std::experimental::coroutine_handle<> _awaiter,
//				const T& _data) : 
//				thread_pool(_thread_pool), 
//				awaiter(_awaiter) {
//		// for send queue
//		data = make_unique<T>(_data);
//	}
//	
//	QueueItem(cppcoro::static_thread_pool* _thread_pool, 
//				std::experimental::coroutine_handle<> _awaiter) : 
//				thread_pool(_thread_pool), 
//				awaiter(_awaiter) {
//		// for recv queue
//		data = make_unique<T>();
//	}

};

template<typename T>
class SendWaitQueue {
	// recv need to co_await this
public:	
	SendWaitQueue(AsyncChannel& host_chan) : host_chan_(host_chan), wait_list(0) {}

	bool await_ready() const noexcept {
		// judge if queue has something
		// TODO: LOCK()
		if (host_chan_.has_pending_data()) {
			host_chan_.dec_pending_data();
			return true;
		}

		host_chan_.inc_starving_data();
		// add recv into wait_queue(just change starve_cnt_ here)
		return false;
	}

	bool await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept {
		// TODO : LOCK()
		// add recv into wait_queue
		host_chan_.recv_wait_queue_.push_back(QueueItem(awaiter));		
		return true;
	}

	T await_resume() noexcept {
		// TODO: LOCK() and UNLOCK() to avoid DEADLOCK!!!
		while (true) {
			// because of non-continuous lock, we have to loop here to make sure we get the data.

			// kind of unlock here
			if (host_chan_.empty())
				continue;
			T ret = std::move(host_chan_.chan_.front());
			host_chan_.chan_.pop();
			break;
		}

		if (!host_chan_.chan_.full() && !wait_list_.empty()) {
			// 1. push
			host_chan_.chan_.push_back(*(wait_list_.front().data));

			// 2. add into pool
			cppcoro::static_thread_pool::schedule_operation resume_coroutine(host_chan_.thread_pool_);
			resume_coroutine.m_awaitingCoroutine = wait_list_.front().awaiter;

			// bug? what if schedule_operation out of bound
			host_chan_.thread_pool_->schedule_impl(&resume_coroutine);
	
			wait_list.pop_front();
		}

		return ret;
	}

private:
	AsyncChannel& host_chan_;
	std::list<QueueItem<T>> wait_list_;
};

template<typename T>
class RecvWaitQueue {
	// sender need to co_await this
public:
	friend class DataDog;

	template<typename T>
	class DataDog {
	public:
		DataDog(RecvWaitQueue& host_queue, T data) : 
				host_queue_(host_queue), data_(data) {}
		
		bool await_ready() const noexcept {
			// judge if recv_wait_queue has something
			//
			// judge if the buffer is full or not
			//
			// TODO : LOCK()

			if (host_queue_.host_chan_.has_starving_data()) {
				host_queue_.host_chan_.dec_starving_data();
				return true;
			}

			host_queue_.host_chan_.inc_pending_data();
			return false;
		}

		void await_suspend(std::experiment::coroutine_handle<> awaiter) noexcept {
			// TODO : LOCK()
			// add send into wait_queue
			host_chan_.send_wait_queue_.push_back(QueueItem(awaiter, data_));
		}

		void await_resume() noexcept {
			// push_data into buffer
			// TODO : LOCK()
			while (true) {
				if (host_queue_.host_chan_.full()) {
					//unlock() and sleep(). ( do we need to sleep here? for single core we must sleep?);
					continue;
				}
				host_queue_.host_chan_.chan_.push_back(data_);
				break;
			}
		} // Done

	private:
		RecvWaitQueue& host_queue_;
		T data_;
	};

	RecvWaitQueue(AsyncChannel& host_chan) : host_chan_(host_chan), wait_list(0) {}

	GetDataDog(const T& data) {
		return DataDog(*this, data);
	}

	/*
	bool await_ready() const noexcept {
		// judge if recv_wait_queue has something
		//
		// judge if the buffer is full or not
		//
		// TODO : LOCK()

		if (host_chan_.has_starving_data()) {
			host_chan_.dec_starving_data();
			return true;
		}

		host_chan_.inc_pending_data();
		return false;
	}

	void await_suspend(std::experiment::coroutine_handle<> awaiter) noexcept {
		// TODO : LOCK()
		// add send into wait_queue
		host_chan_.send_wait_queue.push_back(QueueItem(awaiter));
	}

	void await_resume() noexcept {} // Done
	*/

private:
	AsyncChannel& host_chan_;
	std::list<QueueItem<T>> wait_list_;
};


template<typename T>
class AsyncChannel {
	// need mutex blabla
public:
	friend class SendWaitQueue;
	friend class RecvWaitQueue;

	AsyncChannel(int size) : chan_(size + 1), size_(size), head_(0), tail_(0),
								data_cnt_(0), starve_cnt_(size), send_wait_queue_(*this), recv_wait_queue_(*this) {}
	AsyncChannel(const AsyncChannel&) = delete;
	AsyncChannel(AsyncChannel&&) = delete;
	AsyncChannel& operator=(const AsyncChannel&) = delete;
	AsyncChannel& operator=(AsyncChannel&&) = delete;

	bool empty() {
		return head_ == tail_; 
	}

	bool full() {
		return (tail_ + 1) % size_ == head_;
	}

	bool has_pending_data() {
		return data_cnt_ > 0;
	}

	void dec_pending_data() {
		--data_cnt_;
	}

	void inc_pending_data() {
		++data_cnt_;
	}

	bool has_starving_data() {
		return starve_cnt_ > 0;
	}

	void dec_starving_data() {
		--starve_cnt_;
	}

	void inc_starving_data() {
		++starve_cnt_;
	}

	T& front() {
		return chan_[head_];
	}

	void pop() {
		_inc_head();
	}

	void push_back(const T& data) {
		chan_[tail_] = data;
		_inc_tail();
	}

	cppcoro::task<> send(const T& data) {
		co_await _send(data);
	}

	cppcoro::task<> send(T&& data) {
		co_await _send(std::move(data));				
	}

	cppcoro::task<T> recv() {
		co_return _recv();
	}


private:
	static_thread_pool* thread_pool_;
	vector<T> chan_;
	size_t size_;	// size_ of the chan_
	size_t head_;	// head index
	size_t tail_;	// tail index
	size_t data_cnt_;	// number of data in chan + number of waiting senders 
	size_t starve_cnt_;	// number of starving reciever + number of empty locations in chan_

	SendWaitQueue send_wait_queue_;
	RecvWaitQueue recv_wait_queue_;


	// must be called with lock held
	void _inc_tail() {
		tail_ = (tail_ + 1) % size_;	
	}

	// must be called with lock held
	void _inc_head() {
		head_ = (head_ + 1) % size_;
	}

	// must be called with lock held
	void _dec_tail() {
		tail_ = (tail_ + size_ - 1) % size_;
	}

	// must be called with lock held
	void _dec_head() {
		head_ = (head_ + size_ - 1) % size_;
	}

	cppcoro::task<> _send(T&& data) {
		// first create a datadog, then co_await on it
		co_await recv_wait_queue.GetDataDog(data);
	}

	cppcoro::task<T> _recv() {
		T ret = co_await send_wait_queue;
		co_return ret;
	}

};



