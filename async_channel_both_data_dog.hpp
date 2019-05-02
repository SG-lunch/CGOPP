//#ifndef CPPCORO_CHANNEL_HPP_INCLUDED
//#define CPPCORO_CHANNEL_HPP_INCLUDED

#include <cppcoro/async_manual_reset_event.hpp>
#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/async_mutex.hpp>

#include <experimental/coroutine>
#include <mutex>
#include <atomic>
#include <vector>
#include <list>
#include <condition_variable>

template<typename T>
class AsyncChannel;

template<typename T>
class QueueItem {
public:
	std::unique_ptr<T> data;
	//cppcoro::static_thread_pool* thread_pool;
	std::experimental::coroutine_handle<> awaiter;

	QueueItem(){}
	
	QueueItem(std::experimental::coroutine_handle<> _awaiter,
				const T& _data) : awaiter(_awaiter) {
		// for send queue
		data = std::make_unique<T>(_data);
	}
	
	QueueItem(std::experimental::coroutine_handle<> _awaiter) :  
				awaiter(_awaiter) {
		// for recv queue
		data = std::make_unique<T>();
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
	friend class DataDog;

	template<typename T_>
	class DataDog {
	public:
		DataDog(SendWaitQueue<T_>& host_queue) :
			host_queue_(host_queue), resume_one_(false) {}

		bool await_ready() noexcept {
			std::cout << "[INFO-await_ready:reciever] 1\n"; 

			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			if (host_queue_.host_chan_.has_pending_data()) {
				host_queue_.host_chan_.dec_pending_data();
				
				if (host_queue_.host_chan_.need_append())
					host_queue_.host_chan_.inc_starving_data();

				std::cout << "[INFO-await_ready:reciever] true\n"; 
				resume_one_ = true;
				return true;
			}

			host_queue_.host_chan_.inc_starving_data();
			std::cout << "[INFO-await_ready:reciever] false\n"; 
			// add recv into wait_queue(just change starve_cnt_ here)
			resume_one_ = false;
			return false;
		}
		void await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept {
			// TODO : LOCK()
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			std::cout << "[INFO-await_suspend:reciever] 1\n"; 
			// add recv into wait_queue
			host_queue_.host_chan_.recv_wait_queue_.push_waiter(QueueItem<T_>(awaiter));		
			//return true;
		}

		T_ await_resume() noexcept {
			std::cout << "[INFO-await_resume:reciever] 0\n"; 
			T_ ret;
			std::unique_lock<std::mutex> lck(host_queue_.host_chan_.get_mutex());
			std::cout << "[INFO-await_resume:reciever] 1\n"; 
			while (true) {
				// LOCK()
				if (host_queue_.host_chan_.empty()) {
					// wait on event
					std::cout << "[INFO-await_resume:reciever] 1.3\n"; 
					host_queue_.host_chan_.get_cv_empty().wait(lck);
					std::cout << "[INFO-await_resume:reciever] 1.6\n"; 
					continue;
				}
				std::cout << "[INFO-await_resume:reciever] 1.9\n"; 
				ret = std::move(host_queue_.host_chan_.front());
				host_queue_.host_chan_.pop();
				break;
			}
			std::cout << "[INFO-await_resume:reciever] 2\n"; 

			if (!host_queue_.wait_list_.empty() && resume_one_) {
				// 2. add into pool
				cppcoro::static_thread_pool::schedule_operation resume_coroutine(host_queue_.host_chan_.thread_pool_, host_queue_.wait_list_.front().awaiter);
				host_queue_.host_chan_.thread_pool_->schedule_impl(&resume_coroutine); // bug? what if schedule_operation out of bound
		
				host_queue_.wait_list_.pop_front();
				// final version : only setting it runnable is enough
			}

			std::cout << "[INFO-await_resume:reciever] 3\n"; 
			lck.unlock();
			host_queue_.host_chan_.get_cv_full().notify_one();
			return ret;
		}
	private:
		SendWaitQueue<T_>& host_queue_;
		bool resume_one_;
	};


	DataDog<T> GetDataDog() {
		return DataDog<T>(*this);
	}

	SendWaitQueue(AsyncChannel<T>& host_chan) : host_chan_(host_chan), wait_list_(0) {}
	
	void push_waiter(QueueItem<T>&& waiter) {
		wait_list_.push_back(std::move(waiter));
	}

private:
	AsyncChannel<T>& host_chan_;
	std::list<QueueItem<T>> wait_list_;
};

template<typename T>
class RecvWaitQueue {
	// sender need to co_await this
public:
	friend class DataDog;

	template<typename T_>
	class DataDog {
	public:
		DataDog(RecvWaitQueue<T_>& host_queue, T_ data) : 
				host_queue_(host_queue), data_(data), resume_one_(false) {}
		
		bool await_ready() noexcept {
			// judge if recv_wait_queue has something
			//
			// judge if the buffer is full or not
			//
			// TODO : LOCK()
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			std::cout << "[INFO-await_ready:sender] 1\n"; 

			if (host_queue_.host_chan_.has_starving_data()) {
				//std::cout << "[INFO-await_ready:sender] 2\n"; 
				host_queue_.host_chan_.dec_starving_data();

				if (host_queue_.host_chan_.need_append())
					host_queue_.host_chan_.inc_pending_data();

				std::cout << "[INFO-await_ready:sender] true\n"; 
				resume_one_ = true;
				return true;
			}

			std::cout << "[INFO-await_ready:sender] false\n"; 
			host_queue_.host_chan_.inc_pending_data();
			//std::cout << "[INFO-await_ready:sender] 3.5\n"; 
			resume_one_ = false;
			return false;
		}

		void await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept {
			// TODO : LOCK()
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
		//	std::unique_lock<std::mutex> lck(host_queue_.host_chan_.get_mutex());
		//	cppcoro::async_mutex_lock lock = co_await host_chan_.get_async_mutex().scoped_lock_async();	
			// add send into wait_queue
			host_queue_.host_chan_.send_wait_queue_.push_waiter(QueueItem<T_>(awaiter, data_));
		}

		void await_resume() noexcept {
			// push_data into buffer
			// TODO : LOCK()
			std::unique_lock<std::mutex> lck(host_queue_.host_chan_.get_mutex());
			std::cout << "[INFO-await_resume:sender] 1\n";
			while (true) {		
				//co_await host_chan_.get_async_mutex().lock_async();	
				if (host_queue_.host_chan_.full()) {	
					//wait on full_event
					host_queue_.host_chan_.get_cv_full().wait(lck); // wait will do both of unlock() and lock()
					continue;
				}
				host_queue_.host_chan_.push_back(data_);
				break;
			}

			std::cout << "[INFO-await_resume:sender] 2\n";
			// sender resume, do we need put waiters on waitlist back to runnable?
			//if (host_queue_.host_chan_.starving_data_size() < host_queue_.wait_list_.size()) {
			//if (!host_queue_.wait_list_.empty() && host_queue_.host_chan_.has_pending_data()) {
			if (resume_one_ && !host_queue_.wait_list_.empty()) {
				// add into thread pool
				cppcoro::static_thread_pool::schedule_operation resume_coroutine(host_queue_.host_chan_.thread_pool_, host_queue_.wait_list_.front().awaiter);
				host_queue_.host_chan_.thread_pool_->schedule_impl(&resume_coroutine); // bug? what if schedule_operation out of bound
		
				host_queue_.wait_list_.pop_front();
				// final version : only setting it runnable is enough
			}

			//std::cout << "[INFO-await_resume] three\n";
			lck.unlock();
			//std::cout << "[INFO-await_resume] four\n";
			host_queue_.host_chan_.get_cv_empty().notify_one();
			//std::cout << "[INFO-await_resume] five\n";
		} // Done

	private:
		RecvWaitQueue<T_>& host_queue_;
		T_ data_;
		bool resume_one_;
	};

	RecvWaitQueue(AsyncChannel<T>& host_chan) : host_chan_(host_chan), wait_list_(0) {}

	DataDog<T> GetDataDog(const T& data) {
		return DataDog<T>(*this, data);
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

	void push_waiter(QueueItem<T>&& waiter) {
		wait_list_.push_back(std::move(waiter));
	}

private:
	AsyncChannel<T>& host_chan_;
	std::list<QueueItem<T>> wait_list_;
};


template<typename T>
class AsyncChannel {
public:
	template <typename T_> friend class SendWaitQueue;
	template <typename T_> friend class RecvWaitQueue;

	AsyncChannel(int size, cppcoro::static_thread_pool* p) : chan_(size + 1), size_(size), head_(0), tail_(0),
								data_cnt_(0), starve_cnt_(size), send_wait_queue_(*this),
								recv_wait_queue_(*this), full_event_(true), empty_event_(false),
								thread_pool_(p) {}
	AsyncChannel(const AsyncChannel<T>&) = delete;
	AsyncChannel(AsyncChannel<T>&&) = delete;
	AsyncChannel& operator=(const AsyncChannel<T>&) = delete;
	AsyncChannel& operator=(AsyncChannel<T>&&) = delete;

	size_t size() {
		return (tail_ + size_ - head_) % (size_ + 1);
	}

	size_t capacity() {
		return size_;
	}

	bool empty() {
		return head_ == tail_; 
	}

	bool full() {
		return (tail_ + 1) % (size_ + 1) == head_;
	}

	bool has_pending_data() {
		return data_cnt_ > 0;
	}

	size_t pending_data_size() {
		return data_cnt_;
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

	size_t starving_data_size() {
		return starve_cnt_;
	}

	void dec_starving_data() {
		--starve_cnt_;
	}

	void inc_starving_data() {
		++starve_cnt_;
	}

	bool need_append() {
		if (data_cnt_ + starve_cnt_ < size_)
			return true;
		return false;
	}

	T& front() {
		return chan_[head_];
	}

	void pop() {
		bool full_before = this->full() ? true : false;
		_inc_head();

#ifdef ASYNC_EVENT
		if (!full_event_.is_set())
			full_event_.set();

		if (this->empty())
			empty_event_.reset();	
#else
	//	if (full_before)
	//		cv_full.notify_one();	
#endif
	}

	void push_back(const T& data) {
		chan_[tail_] = data;
		_inc_tail();

#ifdef ASYNC_EVENT
		if (!empty_event_.is_set())
			empty_event_.set();

		if (this->full())
			full_event_.reset();
#else

#endif
	}

	cppcoro::task<> send(const T& data) {
		co_await _send(data);
	}

	/*
	cppcoro::task<> send(T&& data) {
		co_await _send(std::move(data));				
	}*/

	cppcoro::task<T> recv() {
		T ret = co_await _recv();
		co_return ret;
	}

	std::mutex& get_mutex() {
		return mutex_;
	}
	
	cppcoro::async_mutex& get_async_mutex() {
		return mutex_;
	}

	cppcoro::async_manual_reset_event& get_full_event() {
		return full_event_;
	}

	cppcoro::async_manual_reset_event& get_empty_event() {
		return empty_event_;
	}

	std::condition_variable& get_cv_full() {
		return cv_full;
	}

	std::condition_variable& get_cv_empty() {
		return cv_empty;
	}

private:
	cppcoro::static_thread_pool* thread_pool_;
	std::vector<T> chan_;
	size_t size_;	// size_ of the chan_
	size_t head_;	// head index
	size_t tail_;	// tail index
	size_t data_cnt_;	// number of data in chan + number of waiting senders 
	size_t starve_cnt_;	// number of starving reciever + number of empty locations in chan_

	cppcoro::async_mutex async_mutex_;
	cppcoro::async_manual_reset_event full_event_;	// set status : not full, unset status : full
	cppcoro::async_manual_reset_event empty_event_;	// set status : not empty, unset status : empty
	
	std::mutex mutex_;
	std::condition_variable cv_full;
	std::condition_variable cv_empty;

	SendWaitQueue<T> send_wait_queue_;
	RecvWaitQueue<T> recv_wait_queue_;


	// must be called with lock held
	void _inc_tail() {
		tail_ = (tail_ + 1) % (size_ + 1);	
	}

	// must be called with lock held
	void _inc_head() {
		head_ = (head_ + 1) % (size_ + 1);
	}

	// must be called with lock held
	void _dec_tail() {
		tail_ = (tail_ + size_) % (size_ + 1);
	}

	// must be called with lock held
	void _dec_head() {
		head_ = (head_ + size_) % (size_ + 1);
	}

	cppcoro::task<> _send(const T& data) {
		// first create a datadog, then co_await on it
		//std::cout << "[INFO] one _send()\n";
		co_await recv_wait_queue_.GetDataDog(data);
	}

	cppcoro::task<T> _recv() {
		T ret = co_await send_wait_queue_.GetDataDog();
		//T ret = co_await send_wait_queue_;
		co_return ret;
	}

};



