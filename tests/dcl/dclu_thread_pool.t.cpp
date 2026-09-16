//
// Created by Dominic Kloecker on 23/04/2026.
//

#include <barrier>
#include <memory_resource>
#include <unordered_map>
#include <vector>
#include <gtest/gtest.h>

#include "dclu_thread_pool.h"

namespace dcl::test {

const int _HARDWARE_THREADS = std::thread::hardware_concurrency();

TEST(ThreadPool, TasksReturnCorrectResults) {
	ThreadPool                     pool(_HARDWARE_THREADS);
	std::vector<std::future<int> > results;

	for (int i = 0; i < _HARDWARE_THREADS; ++i) {
		auto future = pool.enqueue([i] {
			return i;
		});
		results.emplace_back(std::move(future));
	}

	std::this_thread::sleep_for(std::chrono::milliseconds{100});

	for (int i = 0; i < results.size(); ++i) {
		EXPECT_EQ(results.at(i).get(), i);
	}
}

TEST(ThreadPool, TasksReturnCorrectResultsOnWrapAround) {
	ThreadPool                     pool(2);
	std::vector<std::future<int> > results;

	for (int i = 0; i < _HARDWARE_THREADS * 10; ++i) {
		auto future = pool.enqueue([i] {
			return i;
		});
		results.emplace_back(std::move(future));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds{100});
	for (int i = 0; i < results.size(); ++i) {
		EXPECT_EQ(results.at(i).get(), i);
	}
}

TEST(ThreadPool, CanCointainMoreThreadsThanHardwareLimit) {
	ThreadPool                     pool(_HARDWARE_THREADS * 100);
	std::vector<std::future<int> > results;

	for (int i = 0; i < _HARDWARE_THREADS * 10; ++i) {
		auto future = pool.enqueue([i] {
			return i;
		});
		results.emplace_back(std::move(future));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds{100});
	for (int i = 0; i < results.size(); ++i) {
		EXPECT_EQ(results.at(i).get(), i);
	}
}

TEST(ThreadPool, ConstructorThorwsOnInvalid0Threads) {
	EXPECT_THROW(ThreadPool{0}, std::invalid_argument);
}

TEST(ThreadPool, PoolDrainsTasksOnShutDown) {
	std::vector<std::future<int> > results;
	{
		ThreadPool pool(_HARDWARE_THREADS);
		for (int i = 0; i < _HARDWARE_THREADS; ++i) {
			auto future = pool.enqueue([i] {
				std::this_thread::sleep_for(std::chrono::milliseconds{100});
				return i;
			});
			results.emplace_back(std::move(future));
		}
	}
	for (int i = 0; i < results.size(); ++i) {
		EXPECT_EQ(results.at(i).get(), i);
	}
}

TEST(ThreadPool, PoolForwardsException) {
	ThreadPool                     pool(_HARDWARE_THREADS);
	std::vector<std::future<int> > results;
	for (int i = 0; i < _HARDWARE_THREADS; ++i) {
		auto future = pool.enqueue([i] {
			if (i == 5) throw std::invalid_argument("test");
			return i;
		});
		results.emplace_back(std::move(future));
	}
	for (int i = 0; i < results.size(); ++i) {
		if (i == 5) {
			EXPECT_THROW((void) results.at(i).get(), std::invalid_argument);
		} else {
			// All other tasks should have finished like normal
			EXPECT_EQ(results.at(i).get(), i);
		}
	}
}

TEST(ThreadPool, PoolUsesAllWorkerThreads) {
	const unsigned                  N = std::min(std::thread::hardware_concurrency(), 8u);
	ThreadPool                      pool(N);
	std::mutex                      mtx;
	std::set<std::thread::id>       ids;
	std::barrier sync(N);
	std::vector<std::future<void> > fs;
	for (unsigned i = 0; i < N; ++i)
		fs.push_back(pool.enqueue([&] {
			{
				std::scoped_lock lk(mtx);
				ids.insert(std::this_thread::get_id());
			}
			sync.arrive_and_wait(); // all N must be running at once
		}));
	for (auto &f: fs) f.get();
	EXPECT_EQ(ids.size(), N);
}

TEST(ThreadPool, PoolContainsDemandedNumberOfThreads) {
	// Pool with 100 threads but 10 x number of tasks. Will register total of 100 threads
	const int N = std::min( _HARDWARE_THREADS, 5);
	ThreadPool                     pool(N);
	std::vector<std::future<void> > results;
	std::set<std::thread::id>      threads;
	std::mutex                     mtx;
	for (int i = 0; i < N * 10; ++i) {
		results.push_back(
			pool.enqueue([&mtx, &threads]  {
			std::scoped_lock<std::mutex> lk(mtx);
			threads.insert(std::this_thread::get_id());
		}));
	}
	for (auto &f: results) (void) f.get();
	EXPECT_EQ(threads.size(), N);
}

TEST(ThreadPool, PoolExecutesTasksConcurrently) {
	constexpr int N = 4;
	ThreadPool pool(N);
	constexpr auto T = std::chrono::milliseconds{100};
	const auto start = std::chrono::steady_clock::now();
	std::vector<std::future<void>> fs;
	for (int i = 0; i < N; ++i)
		fs.push_back(pool.enqueue([T] { std::this_thread::sleep_for(T); }));
	for (auto& f : fs) f.get();
	const auto elapsed = std::chrono::steady_clock::now() - start;
	// Each tasks takes 100ms least, so total should be below this
	EXPECT_LT(elapsed, N * T / 2); // ~100ms on 4 threads, not ~400ms
}

} // namespace dcl::test
