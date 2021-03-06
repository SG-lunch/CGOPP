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

template<typename T>
class AsyncChannel;

template<typename T>
class SendWaitQueue;

template<typename T>
class RecvWaitQueue;


template<typename T>
class QueueItem {
public:
	QueueItem(){}

	explicit QueueItem(typename SendWaitQueue<T>::DataDog* sender) : send_data_dog_(sender) {}
	explicit QueueItem(typename RecvWaitQueue<T>::DataDog* receiver) : recv_data_dog_(receiver) {}

	typename RecvWaitQueue<T>::DataDog* get_waiting_send_data_dog() {
		// this is not a bug, because sender is co_awaiting on recv_queue
		return recv_data_dog_;
	}

	typename SendWaitQueue<T>::DataDog* get_waiting_recv_data_dog() {
		// this is not a bug, because recevier is co_awaiting on send_queue
		return send_data_dog_;
	}

private:
	T data_;
	typename SendWaitQueue<T>::DataDog* send_data_dog_;
	typename RecvWaitQueue<T>::DataDog* recv_data_dog_;
};


template<typename T>
class SendWaitQueue {
	// recv need to co_await this
public:	
	friend class DataDog;

	class DataDog {
	public:
		explicit DataDog(SendWaitQueue<T>& host_queue) : host_queue_(host_queue){}

		bool await_ready() noexcept {
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_ready:reciever] 1\n"; 
#endif
			if (!host_queue_.wait_list_.empty()) {
				typename RecvWaitQueue<T>::DataDog* sender = host_queue_.wait_list_.front().get_waiting_send_data_dog();
				data_ = sender->get_data();

				sched_ops_ = std::make_unique<cppcoro::static_thread_pool::schedule_operation>(
								host_queue_.host_chan_.thread_pool_, sender->get_awaiter());
				host_queue_.host_chan_.thread_pool_->schedule_impl(sched_ops_.get());
		
				host_queue_.wait_list_.pop_front();

				if (!host_queue_.host_chan_.empty()) {
					T tmp = std::move(data_);
					data_ = host_queue_.host_chan_.front();
					host_queue_.host_chan_.pop();
					host_queue_.host_chan_.push_back(tmp);
				}

#ifdef DEBUG_MODE
				std::cout << "[INFO-await_ready:reciever] not hang up\n"; 
#endif
				return true;
			} else if (!host_queue_.host_chan_.empty()) {
				data_ = std::move(host_queue_.host_chan_.front());
				host_queue_.host_chan_.pop();

#ifdef DEBUG_MODE
				std::cout << "[INFO-await_ready:reciever] not hang up\n"; 
#endif
				return true;
			}

#ifdef DEBUG_MODE
			std::cout << "[INFO-await_ready:reciever] hangup\n"; 
#endif
			return false;
		}

		bool await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept {
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_suspend:reciever] 1\n"; 
#endif
			if (!host_queue_.wait_list_.empty()) {
				typename RecvWaitQueue<T>::DataDog* sender = host_queue_.wait_list_.front().get_waiting_send_data_dog();
				data_ = sender->get_data();

				sched_ops_ = std::make_unique<cppcoro::static_thread_pool::schedule_operation>(
								host_queue_.host_chan_.thread_pool_, sender->get_awaiter());
				host_queue_.host_chan_.thread_pool_->schedule_impl(sched_ops_.get());
		
				host_queue_.wait_list_.pop_front();

				if (!host_queue_.host_chan_.empty()) {
					T tmp = std::move(data_);
					data_ = host_queue_.host_chan_.front();
					host_queue_.host_chan_.pop();
					host_queue_.host_chan_.push_back(tmp);
				}

#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:reciever] not hang up\n"; 
#endif
				return false;
			} else if (!host_queue_.host_chan_.empty()) {
				data_ = std::move(host_queue_.host_chan_.front());
				host_queue_.host_chan_.pop();

#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:reciever] not hang up\n"; 
#endif
				return false;
			} else {
				awaiter_ = awaiter;
				host_queue_.host_chan_.recv_wait_queue_.push_waiter(QueueItem<T>(this));		
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:reciever] hang up\n"; 
#endif
				return true;
			}
		}

		T await_resume() noexcept {
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_resume:reciever] 0\n"; 
			
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			int waiting_sender = host_queue_.host_chan_.send_wait_queue_.get_wait_list().size();
			int waiting_receiver = host_queue_.host_chan_.recv_wait_queue_.get_wait_list().size();
			std::cout << "[DEBUG] (waiting_sender, waiting_receiver) = (" << waiting_sender << ", " << waiting_receiver << ")\n";
#endif
			return data_;
		}
		
		void set_data(T data) {
			data_ = data;
		}

		T get_data() {
			return data_;
		}

		std::experimental::coroutine_handle<> get_awaiter() {
			return awaiter_;
		}

	private:
		SendWaitQueue<T>& host_queue_;
		std::experimental::coroutine_handle<> awaiter_;
		T data_;
		std::unique_ptr<cppcoro::static_thread_pool::schedule_operation> sched_ops_;
	};


	SendWaitQueue<T>::DataDog GetDataDog() {
		return DataDog(*this);
	}

	SendWaitQueue(AsyncChannel<T>& host_chan) : host_chan_(host_chan), wait_list_(0) {}
	
	void push_waiter(QueueItem<T>&& item) {
		wait_list_.push_back(std::move(item));
	}

	std::list<QueueItem<T>>& get_wait_list() {
		return wait_list_;
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

	class DataDog {
	public:
		DataDog(RecvWaitQueue<T>& host_queue, T data) : 
				host_queue_(host_queue), data_(data) {}
		
		bool await_ready() noexcept {
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_ready:sender] 1\n"; 
#endif
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			
			if (!host_queue_.wait_list_.empty()) {
				typename SendWaitQueue<T>::DataDog* receiver = host_queue_.wait_list_.front().get_waiting_recv_data_dog();	
				receiver->set_data(data_);
			
				// [Potential BUG] should we pop first?
				sched_ops_ = std::make_unique<cppcoro::static_thread_pool::schedule_operation>(
								host_queue_.host_chan_.thread_pool_, receiver->get_awaiter());
				host_queue_.host_chan_.thread_pool_->schedule_impl(sched_ops_.get());
		
				host_queue_.wait_list_.pop_front();
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:sender] not hang up\n"; 
#endif
				return true;
			} else if (!host_queue_.host_chan_.full()) {
				host_queue_.host_chan_.push_back(data_);
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:sender] not hang up\n"; 
#endif
				return true;
			}
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_ready:sender] hang up\n"; 
#endif
			return false;
		}

		bool await_suspend(std::experimental::coroutine_handle<> awaiter) noexcept {
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			
			if (!host_queue_.wait_list_.empty()) {
				typename SendWaitQueue<T>::DataDog* receiver = host_queue_.wait_list_.front().get_waiting_recv_data_dog();	
				receiver->set_data(data_);
				
				sched_ops_ = std::make_unique<cppcoro::static_thread_pool::schedule_operation>(
								host_queue_.host_chan_.thread_pool_, receiver->get_awaiter());
				host_queue_.host_chan_.thread_pool_->schedule_impl(sched_ops_.get());
		
				host_queue_.wait_list_.pop_front();
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:sender] not hang up\n"; 
#endif
				return false;
			} else if (!host_queue_.host_chan_.full()) {
				host_queue_.host_chan_.push_back(data_);
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:sender] not hang up\n"; 
#endif
				return false;
			} else {
				awaiter_ = awaiter;
				host_queue_.host_chan_.send_wait_queue_.push_waiter(QueueItem<T>(this));
#ifdef DEBUG_MODE
				std::cout << "[INFO-await_suspend:sender] hang up\n"; 
#endif
				return true;
			}
		}

		void await_resume() noexcept {
#ifdef DEBUG_MODE
			std::cout << "[INFO-await_resume:sender] 1\n";
			
			std::scoped_lock<std::mutex> lock(host_queue_.host_chan_.get_mutex());
			int waiting_sender = host_queue_.host_chan_.send_wait_queue_.get_wait_list().size();
			int waiting_receiver = host_queue_.host_chan_.recv_wait_queue_.get_wait_list().size();
			std::cout << "[DEBUG] (waiting_sender, waiting_receiver) = (" << waiting_sender << ", " << waiting_receiver << ")\n";
#endif
		}

		void set_data(T data) {
			data_ = data;
		}

		T get_data() {
			return data_;
		}
		
		std::experimental::coroutine_handle<> get_awaiter() {
			return awaiter_;
		}

	private:
		RecvWaitQueue<T>& host_queue_;
		std::experimental::coroutine_handle<> awaiter_;
		T data_;
		std::unique_ptr<cppcoro::static_thread_pool::schedule_operation> sched_ops_;
	};

	RecvWaitQueue(AsyncChannel<T>& host_chan) : host_chan_(host_chan), wait_list_(0) {}

	RecvWaitQueue<T>::DataDog GetDataDog(const T& data) {
		return DataDog(*this, data);
	}

	
	void push_waiter(QueueItem<T>&& item) {
		wait_list_.push_back(std::move(item));
	}
	
	std::list<QueueItem<T>>& get_wait_list() {
		return wait_list_;
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
	
	using sched_op = cppcoro::static_thread_pool::schedule_operation*;

	AsyncChannel(int size, cppcoro::static_thread_pool* p) : chan_(size + 1), size_(size), head_(0), tail_(0),
								data_cnt_(0), starve_cnt_(size), send_wait_queue_(*this),
								recv_wait_queue_(*this), thread_pool_(p) {}
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
		_inc_head();
	}

	void push_back(const T& data) {
		chan_[tail_] = data;
		_inc_tail();
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
	
private:
	cppcoro::static_thread_pool* thread_pool_;
	std::vector<T> chan_;
	size_t size_;	// size_ of the chan_
	size_t head_;	// head index
	size_t tail_;	// tail index
	size_t data_cnt_;	// number of data in chan + number of waiting senders 
	size_t starve_cnt_;	// number of starving reciever + number of empty locations in chan_
	
	std::mutex mutex_;

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
		co_await recv_wait_queue_.GetDataDog(data);
	}

	cppcoro::task<T> _recv() {
		T ret = co_await send_wait_queue_.GetDataDog();
		co_return ret;
	}

};

