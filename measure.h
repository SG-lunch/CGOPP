//try to make a generic programming
#include <chrono>
#include <functional>

//type define unsign long ulong;
using namespace std::chrono;
class Measure {
public:
	Measure() : m_time{high_resolution_clock::now()} {}

	~Measure() = default;
	
	void set_time(high_resolution_clock::time_point ntime) {
		this->m_time = ntime;
	}

	high_resolution_clock::time_point get_time() {
		return this->m_time;
	}

	auto diff_time_second(const Measure& m1) {
		return duration_cast<seconds>((this->m_time) - (m1.m_time));
	}

	auto diff_time_second(const Measure* m1) {
		return duration_cast<seconds>((this->m_time) - (m1->m_time));
	}
	auto diff_time_millisec(const Measure& m1) {
		return duration_cast<milliseconds>((this->m_time) - (m1.m_time));
	}

	auto diff_time_millisec(const Measure* m1) {
		return duration_cast<milliseconds>((this->m_time) - (m1->m_time));
	}

private:
	high_resolution_clock::time_point m_time;
	//std::chrono::high_resolution_clock::time_point m_time;
	// can add some other things like memory blabla
};

template<typename T>
class TestCase {
public:
	void test(void (*f)(T&, int), T& obj, int N) {
		Measure m1;
		(*f)(obj, N);
		Measure m2;
		//std::cout << "time: " << m2.diff_time_millisec(m1).count() << " msec\n";
		std::cout << "time: " << m2.diff_time_second(m1).count() << " sec\n";
				
	}
	void test(T* obj, void (T::*f)()) {
		Measure m1;
		(obj->*f)();
		Measure m2;
		//std::cout << "time: " << m2.diff_time_millisec(m1).count() << " msec\n";
		std::cout << "time: " << m2.diff_time_second(m1).count() << " sec\n";
	}
private:
};
