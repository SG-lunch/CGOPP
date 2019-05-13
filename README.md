# CGOPP
##We implemented async channel in cpp based on coroutine.
files:
1. async\_channel\_both\_data\_dog.hpp is our main header file.
	
2. test\_thread\_pool.cpp is a sample test file.
	
3. cppcoro : this is facebook coroutine library (https://github.com/lewissbaker/cppcoro) with little modifications by ourself.
	
4. sync\_channel.hpp, cgo\_scheduler.hpp are future work.

5. cppcoro, facebook coroutine library with slightly modifications. https://github.com/lewissbaker/cppcoro.

5. etc.


compile :
	clang++ -std=c++17 -fcoroutines-ts target.cpp -stdlib=libc++ -L/path-to-lib/cppcoro/build/linux_x64_clang9.0.0_optimised/lib -I/path-to-include/cppcoro/include -lcppcoro -lpthread

source :
	ThreadPool.h is from https://github.com/progschj/ThreadPool/
