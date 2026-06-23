#include "dsl_thread_pool.h"
#include <format>

namespace dsl {
void ThreadPool::worker() {
	while (true) {
		std::function< void()> current_task;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait(lock, [this]() { return stop_.test() || !queue_.empty(); });

			if (stop_.test() &&queue_.empty()) {
				break;
			}
			if (queue_.empty()) {
				continue;
			}
			current_task = queue_.front();
			queue_.pop();
		}
		current_task();
	}
}

ThreadPool::ThreadPool(const std::size_t nr_threads) {
	if (nr_threads == 0) {
		throw std::invalid_argument(
			std::format(
				"Requested number of threads: {}. Must be > 0",
				nr_threads,
				std::thread::hardware_concurrency()
			)
		);
	}
	for (size_t i = 0; i < nr_threads; i++) {
		workers_.emplace_back(&ThreadPool::worker, this);
	}
}

ThreadPool::~ThreadPool() {
	{
		std::unique_lock<std::mutex> lock(mutex_);
		stop_.test_and_set(std::memory_order_seq_cst);
	}
	cv_.notify_all();
	for (auto& worker : workers_) {
		worker.join();
	}
}
}
