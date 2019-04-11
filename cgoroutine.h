#ifndef CPP_GO_ROUTINE_H
#define CPP_GO_ROUTINE_H

#include <cppcoro/task.hpp>
#include <mutex>

namespace sgwf {
class Scheduler {
	// singleton or not?
public:
	Scheduler() = default;
	~Scheduler() = default;

	Scheduler(unsigned nproc) : nproc_(nproc) {}

	void Schedule() {
		// DelayedScheduler concept
		// for those thread to run
	}

private:
	/*
	 * static Scheduler& getInstance() {
	 *	// local static singleton. best way in cpp to write singleton
	 *		static Scheduler scheduler;
	 *		return scheduler;
	 * }
	 */





	unsigned nproc_;	// set by user
	unsigned cgo_num_;
	unsigned thread_num_;	// we may need a thread pool to organize those threads
	// global cgo_queue_; vector<cppcoro::task<>> something like that
	// we need a queue for each nproc, think about what data structure to use

};

}
