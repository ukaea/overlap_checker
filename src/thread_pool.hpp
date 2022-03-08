#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
#include <mutex>
#include <vector>


/* simple thread pool, threads are created in the constructor and joined in
 * the destructor. you can submit() lambdas and they'll be executed
 * asynchronously.
 *
 * note that this class isn't very user friendly, you probably want to use one
 * of the helpers below to make sure these tasks get executed by some
 * deterministic point
 */
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

/* modelled after a "parallel for loop", you submit() jobs in a for loop,
 * these are executed by the pool, and this code waits for all submitted jobs
 * to execute during the destructor
 */
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
			cond.notify_all();
		});

		std::unique_lock<std::mutex> mlock(mutex);
		num_inflight += 1;
	}
};

/* modelled after map_async from Python's multiprocessing library. you
 * submit() jobs and these are executed by the pool, and this code allows you
 * to get results from each job as soon as it completes.
 *
 * note that this is likely to be in a different order than they were
 * submitted
 */
template<typename T>
class asyncmap {
	std::mutex mutex;
	std::condition_variable cond_res;
	std::condition_variable cond_done;
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
			cond_done.wait(mlock);
		}
	}

	void submit(thread_pool &pool, std::function<T(void)> fn) {
		pool.submit([this, fn]() {
			auto result = fn();

			std::unique_lock<std::mutex> mlock(mutex);
			num_inflight -= 1;
			results.emplace_back(result);
			cond_res.notify_one();
			if (num_inflight == 0) {
				cond_done.notify_all();
			}
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
			cond_res.wait(mlock);
		}
		auto result = results.front();
		results.pop_front();
		return result;
	}
};
