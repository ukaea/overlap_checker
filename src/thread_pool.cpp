#include "thread_pool.hpp"

#ifdef INCLUDE_TESTS
#include <catch2/catch_test_macros.hpp>
#endif

thread_pool::thread_pool(size_t num_workers) : running(true)
{
	for (size_t i = 0; i < num_workers; i++) {
		workers.emplace_back(&thread_pool::worker, this);
	}
}

thread_pool::~thread_pool()
{
	running = false;

	for (auto &worker : workers) {
		// I think I might be able to get away with doing this once! but
		// given the costs it doesn't seem worth it
		cond.notify_all();

		worker.join();
	}
}

void thread_pool::worker()
{
	std::unique_lock<std::mutex> mlock(mutex);

	while(running) {
		if (queue.empty()) {
			cond.wait(mlock);
			continue;
		}

		auto task = queue.front();
		queue.pop_front();
		mlock.unlock();
		task();
		mlock.lock();
	}
}

void thread_pool::submit(std::function<void(void)> task)
{
	std::unique_lock<std::mutex> mlock(mutex);

	queue.emplace_back(task);
	cond.notify_one();
}


#ifdef INCLUDE_TESTS
TEST_CASE("thread_pool") {
	thread_pool pool(1);

	SECTION("parfor waits") {
		parfor work;
		bool completed = false;
		// submit a simple job
		work.submit(pool, [&completed]() {
			completed = true;
		});
		// wait for them all to complete
		work.wait();
		// make sure our job ran
		CHECK(completed);
	};

	SECTION("asyncmap") {
		asyncmap<int> map;
		constexpr int N = 5;
		// run a few jobs asynchronously
		for (size_t i = 0; i < N; i++) {
			map.submit(pool, [i]() {
				return i;
			});
		}
		// collect their results
		int done[N] = {};
		while (!map.empty()) {
			done[map.get()] += 1;
		}
		// make sure they all ran once
		for (int d : done) {
			CHECK(d == 1);
		}
	};
}
#endif
