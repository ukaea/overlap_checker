#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
#include <mutex>
#include <vector>


class thread_pool {
	std::vector<std::thread> workers;

	std::mutex mutex;
	std::condition_variable cond;
	std::deque<std::function<void(void)>> queue;
	bool running;

	void worker();

public:
	thread_pool(size_t num_workers);
	virtual ~thread_pool();

	void submit(std::function<void(void)> task);
};

class parfor {
	std::mutex mutex;
	std::condition_variable cond;

	size_t num_inflight;

public:
	parfor() : num_inflight{0} {}

	virtual ~parfor() {
		wait();
	}

	void wait() {
		std::unique_lock<std::mutex> mlock(mutex);
		while(num_inflight > 0) {
			cond.wait(mlock);
		}
	}

	void submit(thread_pool &pool, std::function<void(void)> fn) {
		pool.submit([this, fn]() {
			fn();

			std::unique_lock<std::mutex> mlock(mutex);
			num_inflight -= 1;
			mlock.unlock();

			cond.notify_one();
		});

		std::unique_lock<std::mutex> mlock(mutex);
		num_inflight += 1;
	}
};

template<typename T>
class asyncmap {
	std::mutex mutex;
	std::condition_variable cond;
	std::deque<T> results;

	size_t num_inflight;

public:
	asyncmap() : num_inflight{0} {}

	virtual ~asyncmap() {
		// need to wait for everything because they all have a reference to
		// this and would likely crash when they tried to store their result
		wait();
	}

	void wait() {
		std::unique_lock<std::mutex> mlock(mutex);
		while(num_inflight > 0) {
			cond.wait(mlock);
		}
	}

	void apply(thread_pool &pool, std::function<T(void)> fn) {
		pool.submit([this, fn]() {
			auto result = fn();

			std::unique_lock<std::mutex> mlock(mutex);
			num_inflight -= 1;
			results.emplace_back(result);
			cond.notify_one();
		});

		std::unique_lock<std::mutex> mlock(mutex);
		num_inflight += 1;
	}

	bool empty() {
		std::unique_lock<std::mutex> mlock(mutex);
		return results.empty() && num_inflight == 0;
	}

	T get() {
		std::unique_lock<std::mutex> mlock(mutex);
		while (results.empty()) {
			cond.wait(mlock);
		}
		auto result = results.front();
		results.pop_front();
		return result;
	}
};
