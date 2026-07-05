//
// Created by Dominic Kloecker on 03/04/2026.
//
#include <fstream>

#include "dsl/logging/dsl_async_logger.h"

#include <iostream>

#include "dsl/logging/dsl_logger_utils.h"

namespace dsl {
template<size_t QueueCapacity, size_t FlushThreshold>
std::expected<void, lifecycle::Error> AsyncLogger<QueueCapacity, FlushThreshold>::on_start() {
	std::println("LOGGER STARTING");

	// Open the log file only if file logging is enabled.
	if (config_.enable_file) {
		const auto &path = config_.log_file;
		if (path.has_parent_path()) {
			std::filesystem::create_directories(path.parent_path());
		}
		log_file_.rdbuf()->pubsetbuf(stream_buffer_, STREAM_BUFFER_SIZE);
		log_file_.open(path, std::ios::out | std::ios::app);
		if (!log_file_.is_open()) {
			return std::unexpected{
				lifecycle::Error{
					.rc      = lifecycle::ErrorCode::FAILED_TRANSITION,
					.details = "AsyncLogger: failed to open log file"
				}
			};
		}
	}

	// Pick Handling based off desired back pressure policy
	switch (config_.back_pressure_policy) {
		case BackPressurePolicy::e_BLOCK: {
			enqueue_handler_ = &enqueue_block;
		}
		break;
		case BackPressurePolicy::e_DROP: {
			enqueue_handler_ = &enqueue_drop;
		}
		break;
		case BackPressurePolicy::e_DROP_BELOW_LEVEL: {
			enqueue_handler_ = &enqueue_drop_below_level;
		}
		break;
	}

	// Launch Consumer Thread
	consumer_thread_ = std::thread([this] {
		while (!this->stop_.stop_requested() || !this->queue_.empty()) {
			LogRecord record;
			// We can go back to sleep if nothing is happening
			if (this->queue_.empty() || !this->queue_.pop(record)) {
				std::this_thread::yield();
				continue;
			}

			// Fan out the record to each enabled sink whose level filter accepts it.
			if (this->config_.enable_file && record.level <= this->min_file_level()) {
				write_log(this->log_file_, this->config_.format, record);
				// Flush file immediately on errors so they survive a crash.
				if (record.level <= LogLevel::e_ERROR) {
					this->log_file_.flush();
				}
			}
			if (this->config_.enable_cout && record.level <= this->min_cout_level()) {
				write_log(std::cout, this->config_.format, record);
			}
		}
		// Flush remaining file records before shutdown.
		if (this->config_.enable_file) {
			this->log_file_.flush();
		}
	});
	return {};
}

template<size_t QueueCapacity, size_t FlushThreshold>
std::expected<void, lifecycle::Error> AsyncLogger<QueueCapacity, FlushThreshold>::on_stop() {
	stop_.request_stop();
	if (consumer_thread_.joinable()) {
		consumer_thread_.join();
	}
	if (log_file_.is_open()) {
		log_file_.flush();
		log_file_.close();
	}
	// Revert to the no-op sink
	enqueue_handler_ = &enqueue_none;
	return {};
}


template<size_t QueueCapacity, size_t FlushThreshold>
void AsyncLogger<QueueCapacity, FlushThreshold>::init(LogConfig config) {
	config_ = std::move(config);
	(void) this->start();
}

template<size_t QueueCapacity, size_t FlushThreshold>
void AsyncLogger<QueueCapacity, FlushThreshold>::log(const LogLevel                level,
                                                     const std::string_view      message,
                                                     const std::source_location &loc) {
	// Skip enqueue if no enabled sink would accept this record.
	const bool will_write_file = config_.enable_file && level <= config_.min_log_file_level;
	const bool will_write_cout = config_.enable_cout && level <= config_.min_cout_level;
	if (!will_write_file && !will_write_cout) return;
	LogRecord record{};
	record.level = level;
	// Ensure that we truncate the log message where necessary
	record.message_length = std::min(message.length(), MAX_MESSAGE_LENGTH);
	std::memcpy(record.message, message.data(), record.message_length);
	record.location = loc;
	// Enqueue onto system based on chosen Policy
	enqueue_handler_(*this, record);
}

#ifdef TESTING
template<size_t QueueCapacity, size_t FlushThreshold>
void AsyncLogger<QueueCapacity, FlushThreshold>::reset() {
	(void) this->stop();
	stop_ = std::stop_source{};
}

// Small Queue for Tests
template class AsyncLogger<4, 32>;
#endif

template class AsyncLogger<1024, 64>;
}


