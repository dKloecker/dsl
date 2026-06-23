//
// Created by Dominic Kloecker on 13/06/2026.
//

#ifndef DSL_THREAD_POOL_H
#define DSL_THREAD_POOL_H

#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <functional>

namespace dsl {
// https://dev.to/ish4n10/making-a-thread-pool-in-c-from-scratch-bnm
// TODO: Docs
class ThreadPool {
	std::vector<std::jthread>          workers_;
	std::mutex                         mutex_;
	std::condition_variable            cv_;
	std::atomic_flag				   stop_{};
	std::queue<std::function<void()> > queue_;

	void worker();
public:
	explicit ThreadPool(std::size_t nr_threads = std::thread::hardware_concurrency());
	~ThreadPool();

	template<typename Fn, typename... Args>
	auto enqueue(Fn &&f, Args &&... args) -> std::future<decltype(f(args...))>;

	ThreadPool(ThreadPool &)                  = delete;
	ThreadPool(const ThreadPool &)            = delete;
	ThreadPool &operator=(ThreadPool &&)      = delete;
	ThreadPool &operator=(const ThreadPool &) = delete;
};

template<typename Fn, typename... Args>
auto ThreadPool::enqueue(Fn &&f, Args &&... args) -> std::future<decltype(f(args...))> {
	using return_type = decltype(f(args...));
	auto func             = std::bind(std::forward<Fn>(f), std::forward<Args>(args)...);
	auto encapsulated_ptr = std::make_shared<std::packaged_task<return_type()> >(func);

	std::future<return_type> future_object = encapsulated_ptr->get_future();
	{
		std::unique_lock lock(mutex_);
		// Execute the Function
		queue_.emplace([encapsulated_ptr](){(*encapsulated_ptr)();});
	}
	cv_.notify_one();
	return future_object;
}

}

#endif //DSL_THREAD_POOL_H
