//
// Created by Dominic Kloecker on 03/04/2026.
//

#ifndef DSL_DALL_ASYNC_LOGGER_H_
#define DSL_DALL_ASYNC_LOGGER_H_

#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <source_location>
#include <thread>

#include "dall_log_record.h"
#include "dclc_bounded_queue.h"
#include "dclu_life_timed.h"

namespace dal {

/**
 * @brief Singleton lock-free asynchronous logger.
 *
 * Log records are placed onto an SPSC queue by the producing thread and
 * written to a log file by a dedicated consumer thread.
 * The output stream is buffered, but @c FATAL and @c ERROR records trigger an immediate flush.
 *
 * @tparam QueueCapacity     Capacity of the underlying SPSC queue
 * @tparam FlushThreshold    Number of messages worth of stream buffer space.
 */
template<size_t QC = log_defaults::QUEUE_CAPACITY, size_t FT = log_defaults::FLUSH_THRESHOLD>
class AsyncLogger : public dcl::LifeTimed {
	static constexpr size_t STREAM_BUFFER_SIZE = FT * log_defaults::MAX_MESSAGE_LENGTH;
	LogConfig               config_{};

	std::thread      consumer_thread_{};
	std::stop_source stop_{};

	char                      stream_buffer_[STREAM_BUFFER_SIZE]{};
	dcl::b_mpmc_q<LogRecord, QC>	   queue_{};
	std::ofstream					   log_file_{};

	AsyncLogger() = default;

protected:
	/**
	 * @brief LifeTimed implementation
	 */
	[[nodiscard]] std::optional<Error> runStart() override;
	[[nodiscard]] std::optional<Error> runStop() override;
private:

	/**
	 * Handler for enqueuing Log Record based on Loggers configured BackPressure policy.
	 * Defaults to void function when uninitialized
	 */
	using enqueue_fn = void (*)(AsyncLogger &, const LogRecord &);

	// No-op sink
	static void enqueue_none(AsyncLogger &, const LogRecord &) {}

	enqueue_fn enqueue_handler_ = &enqueue_none;

	static void enqueue_block(AsyncLogger &self, const LogRecord &record) {
		while (!self.queue_.push(record)) {}
	}

	static void enqueue_drop(AsyncLogger &self, const LogRecord &record) {
		self.queue_.push(record);
	}

	static void enqueue_drop_below_level(AsyncLogger &self, const LogRecord &record) {
		if (record.level > self.config_.drop_threshold) {
			self.queue_.push(record);
			return;
		}
		while (!self.queue_.push(record)) {}
	}

public:
	static constexpr size_t QUEUE_CAPACITY     = QC;
	static constexpr size_t FLUSH_THRESHOLD    = FT;
	static constexpr size_t MAX_MESSAGE_LENGTH = log_defaults::MAX_MESSAGE_LENGTH;

	~AsyncLogger() override {
		(void) this->stop();
	}

	AsyncLogger(const AsyncLogger &) = delete;

	AsyncLogger &operator=(const AsyncLogger &) = delete;

	AsyncLogger(AsyncLogger &&) = delete;

	AsyncLogger &operator=(AsyncLogger &&) = delete;

#ifdef DSL_TESTING
	/// @brief Shuts down the logger and resets the internal state. Test-only.
	void reset();
#endif

	/** Access to Logger Instance */
	static AsyncLogger &instance() {
		static AsyncLogger logger;
		return logger;
	}

	/**
	 * Initialize logger based  provided configuration.
	 * Must be called before any logs are written.
	 * @param config for Logging
	 * @throws std::runtime_error on failure
	 */
	void init(LogConfig config);

	/**
	 * @return current minimum log level
	 */
	LogLevel min_file_level() const {
		return config_.min_log_file_level;
	}

	/**
	 * Update the minimum log level
	 * @param level new minimum log level
	 */
	void set_min_file_level(const LogLevel level) {
		config_.min_log_file_level = level;
	}

	/**
	 * @return current minimum log level
	 */
	LogLevel min_cout_level() const {
		return config_.min_cout_level;
	}

	/**
	 * Update the minimum log level
	 * @param level new minimum log level
	 */
	void set_min_cout_level(const LogLevel level) {
		config_.min_cout_level = level;
	}

	/**
	 * @brief Enqueue a log record.
	 *
	 * Captures the current timestamp and source location, constructs a
	 * @c LogRecord, and enqueues it according to the configured backpressure policy.
	 * Messages longer than @c MAX_MESSAGE_LENGTH are truncated.
	 *
	 * @param level   Severity of this log entry
	 * @param message Log message content
	 * @param loc     Source location (captured automatically at the call site).
	 */
	void log(LogLevel                    level,
	         std::string_view            message,
	         const std::source_location &loc = std::source_location::current());

	void trace(const std::string_view message, const std::source_location &loc = std::source_location::current()) {
		log(LogLevel::e_TRACE, message, loc);
	};

	void debug(const std::string_view message, const std::source_location &loc = std::source_location::current()) {
		log(LogLevel::e_DEBUG, message, loc);
	};

	void info(const std::string_view message, const std::source_location &loc = std::source_location::current()) {
		log(LogLevel::e_INFO, message, loc);
	};

	void warn(const std::string_view message, const std::source_location &loc = std::source_location::current()) {
		log(LogLevel::e_WARN, message, loc);
	};

	void error(const std::string_view message, const std::source_location &loc = std::source_location::current()) {
		log(LogLevel::e_ERROR, message, loc);
	}

	void fatal(const std::string_view message, const std::source_location &loc = std::source_location::current()) {
		log(LogLevel::e_FATAL, message, loc);
	};
};

/// @brief Default logger type alias.
using DefaultAsyncLogger = AsyncLogger<>;

/// @name Convenience logging macros
/// Support @c std::format syntax. Source location is captured automatically.
/// @{
#define LOG_DEBUG(FMT, ...) ::dal::DefaultAsyncLogger::instance().debug(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
#define LOG_INFO(FMT, ...) ::dal::DefaultAsyncLogger::instance().info(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
#define LOG_WARN(FMT, ...) ::dal::DefaultAsyncLogger::instance().warn(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
#define LOG_ERROR(FMT, ...) ::dal::DefaultAsyncLogger::instance().error(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
#define LOG_FATAL(FMT, ...) ::dal::DefaultAsyncLogger::instance().fatal(std::format(FMT __VA_OPT__(,) __VA_ARGS__))
///@}
}

#endif  // DSL_DALL_ASYNC_LOGGER_H_
