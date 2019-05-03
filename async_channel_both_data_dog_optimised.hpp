#include <cppcoro/async_manual_reset_event.hpp>
#include <cppcoro/static_thread_pool.hpp>
#include <cppcoro/async_mutex.hpp>

#include <experimental/coroutine>
#include <mutex>
#include <atomic>
#include <vector>
#include <list>
#include <condition_variable>

//#define DEBUG_MODE
// we have to release the memory in destructor

template<typename T>
class AsyncChannel;

template<typename T>
class QueueItem {
public:
	//cppcoro::static_thread_pool* thread_pool;
	std::experimental::coroutine_handle<> awaiter;

	QueueItem(){}
	
	QueueItem(std::experimental::coroutine_handle<> _awaiter,
				const T& data) : awaiter(_awaiter), data_(data) {
		// for send queue
		//data_ = std::make_unique<T>(data);
	}
	
	QueueItem(std::experimental::coroutine_handle<> _awaiter) :  
				awaiter(_awaiter) {
		// for recv queue
		//data_ = std::make_unique<T>();
	}
	
	void set_data(T data) {
		data_ = data;
	}

	T get_data() {
		return data_;
	}
private:
	T data_;
};


template<typename T>
class SendWaitQueue {
	// recv need to co_await this
public:	
	friend class DataDog;

	template<typename T_>
	class DataDog {
	public:
		explicit DataDog(SendWaitQueue<T_>& host_queue) :
			host_queue_(host_queue), resume_one_(false) {}

		bool await_ready() noexcept {
			// feel like we only need to do it in suspend, i.e., we can just return false here to get into suspend
			/*
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			
			if (!host_queue_.host_chan_.empty()) {
				// if buffer has something
				data_ = std::move(host_queue_.host_chan_.front());
				host_queue_.host_chan_.pop();

				// if we have sender waiting for this
				if (!host_queue_.wait_list_.empty()) {
					cppcoro::static_thread_pool::schedule_operation* resume_coroutine = 
						host_queue_.host_chan_.get_sched_op(host_queue_.host_chan_.thread_pool_,
								host_queue_.wait_list_.front().awaiter);
					host_queue_.host_chan_.thread_pool_->schedule_impl(resume_coroutine); // bug? what if schedule_operation out of bound
			
					host_queue_.wait_list_.pop_front();
				}
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_ready:reciever] true\n"; 
#endif
				return true;
			} else {
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_ready:reciever] false\n"; 
#endif
				return false;
			}
			*/
			return false;
		}
		bool await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept {
			// TODO : LOCK()
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_suspend:reciever] 1\n"; 
#endif
			// because our lock is not consecutive, so we have to check it again here.
			if (!host_queue_.host_chan_.empty()) {
				// if buffer has something
				data_ = std::move(host_queue_.host_chan_.front());
				host_queue_.host_chan_.pop();

				// if we have sender waiting for this
				if (!host_queue_.wait_list_.empty()) {
					host_queue_.host_chan_.push_back(host_queue_.wait_list_.front()->get_data());

					cppcoro::static_thread_pool::schedule_operation* resume_coroutine = 
						host_queue_.host_chan_.get_sched_op(host_queue_.host_chan_.thread_pool_,
								host_queue_.wait_list_.front()->get_awaiter());
					host_queue_.host_chan_.thread_pool_->schedule_impl(resume_coroutine); // bug? what if schedule_operation out of bound
			
					host_queue_.wait_list_.pop_front();
				}
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:reciever] not hang on\n"; 
#endif
				return false;
			} else {
				// add recv into wait_queue
				awaiter_ = awaiter;
				host_queue_.host_chan_.recv_wait_queue_.push_waiter(this);		
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:reciever] hang on\n"; 
#endif
				return true;
			}
		}

		T_ await_resume() noexcept {
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_resume:reciever] 0\n"; 
#endif
			return data_;
		}
		
		void set_data(T_ data) {
			data_ = data;
		}

		T_ get_data() {
			return data_;
		}

		std::experimental::coroutine_handle<> get_awaiter() {
			return awaiter_;
		}

	private:
		SendWaitQueue<T_>& host_queue_;
		std::experimental::coroutine_handle<> awaiter_;
		T_ data_;
		bool resume_one_;
	};


	DataDog<T> GetDataDog() {
		return DataDog<T>(*this);
	}

	SendWaitQueue(AsyncChannel<T>& host_chan) : host_chan_(host_chan), wait_list_(0) {}
	
	//void push_waiter(QueueItem<T>&& waiter) {
	//	wait_list_.push_back(std::move(waiter));
	//}
	
	void push_waiter(SendWaitQueue<T>::DataDog<T>* waiter) {
		wait_list_.push_back(waiter);
	}

private:
	AsyncChannel<T>& host_chan_;
	//std::list<QueueItem<T>> wait_list_;
	std::list<SendWaitQueue<T>::DataDog<T>*> wait_list_;
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
			/*
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_ready:sender] 1\n"; 
#endif
			if (!host_queue_.host_chan_.full()) {
				// if buffer is full
				host_queue_.host_chan_.push_back(data_);
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_ready:sender] true\n"; 
#endif
				return true;
			} else if (!host_queue_.wait_list_.empty()) {
				// do we have waiters
				host_queue_.wait_list_.front().set_data(data_);
				
				// put recv back runable
				//
				// TODO : pop first, put runable later
				cppcoro::static_thread_pool::schedule_operation* resume_coroutine = 
					host_queue_.host_chan_.get_sched_op(host_queue_.host_chan_.thread_pool_, 
							host_queue_.wait_list_.front().awaiter);
				host_queue_.host_chan_.thread_pool_->schedule_impl(resume_coroutine); // bug? what if schedule_operation out of bound
		
				host_queue_.wait_list_.pop_front();
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_ready:sender] true\n"; 
#endif
				return true;
			} else {
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_ready:sender] false\n"; 
#endif
				return false;
			}
			*/
			return false;
		}

		bool await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept {
			// TODO : LOCK()
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			
			if (!host_queue_.host_chan_.full()) {
				// if buffer is not full
				host_queue_.host_chan_.push_back(data_);
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:sender] not hang up\n"; 
#endif
				return false;
			} else if (!host_queue_.wait_list_.empty()) {
				// do we have waiters?
				//host_queue_.wait_list_.front().set_data(data_);
				host_queue_.wait_list_.front()->set_data(data_);
				
				// put recv back runable
				//
				// TODO : pop first, put runable later
				cppcoro::static_thread_pool::schedule_operation* resume_coroutine = 
					host_queue_.host_chan_.get_sched_op(host_queue_.host_chan_.thread_pool_, 
							host_queue_.wait_list_.front()->get_awaiter());
				host_queue_.host_chan_.thread_pool_->schedule_impl(resume_coroutine); // bug? what if schedule_operation out of bound
		
				host_queue_.wait_list_.pop_front();
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:sender] not hang up\n"; 
#endif
				return false;
			} else {
				// add send into wait_queue
				awaiter_ = awaiter;
				host_queue_.host_chan_.send_wait_queue_.push_waiter(this);
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:sender] hang up\n"; 
#endif
				return true;
			}
		}

		void await_resume() noexcept {
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_resume:sender] 1\n";
#endif
		} // Done

		void set_data(T_ data) {
			data_ = data;
		}

		T_ get_data() {
			return data_;
		}
		
		std::experimental::coroutine_handle<> get_awaiter() {
			return awaiter_;
		}

	private:
		RecvWaitQueue<T_>& host_queue_;
		std::experimental::coroutine_handle<> awaiter_;
		T_ data_;
		bool resume_one_;
	};

	RecvWaitQueue(AsyncChannel<T>& host_chan) : host_chan_(host_chan), wait_list_(0) {}

	DataDog<T> GetDataDog(const T& data) {
		return DataDog<T>(*this, data);
	}

	/*
	void push_waiter(QueueItem<T>&& waiter) {
		wait_list_.push_back(std::move(waiter));
	}
	*/
	
	void push_waiter(RecvWaitQueue<T>::DataDog<T>* waiter) {
		wait_list_.push_back(waiter);
	}

private:
	AsyncChannel<T>& host_chan_;
	//std::list<QueueItem<T>> wait_list_;
	std::list<RecvWaitQueue<T>::DataDog<T>*> wait_list_;
};


template<typename T>
class AsyncChannel {
public:
	template <typename T_> friend class SendWaitQueue;
	template <typename T_> friend class RecvWaitQueue;
	//template <typename T_> friend class SendWaitQueue<T_>::DataDog;
	//template <typename T_> friend class RecvWaitQueue<T_>::DataDog;
	
	using sched_op = cppcoro::static_thread_pool::schedule_operation*;

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

	sched_op get_sched_op(cppcoro::static_thread_pool* tp, std::experimental::coroutine_handle<> awaiter) noexcept {
		sched_op p = new cppcoro::static_thread_pool::schedule_operation(tp, awaiter);
		sched_ops_.push_back(p);
		return p;
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

	std::vector<sched_op> sched_ops_;

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



