//
// Created by Dominic Kloecker on 03/04/2026.
//

#ifndef DSL_DALL_LOG_RECORD_H_
#define DSL_DALL_LOG_RECORD_H_

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>

#include "dall_logger_enums.h"

namespace dal {
namespace log_defaults {
inline constexpr size_t MAX_MESSAGE_LENGTH = 1024;
inline constexpr size_t QUEUE_CAPACITY     = 1024;
inline constexpr size_t FLUSH_THRESHOLD    = 64;
}

// TODO: Maybe Move behind manager
// TODO: Add different logger so I can have logger per component maybe?

/**
 * @brief A single log entry to be enqueued and written by the consumer thread
 */
struct LogRecord {
	LogLevel             level                                     = LogLevel::e_INFO;
	size_t               message_length                            = 0;
	char                 message[log_defaults::MAX_MESSAGE_LENGTH] = {};
	std::source_location location{};
	// TODO: use a different time system here
	std::chrono::time_point<std::chrono::system_clock> time_stamp = std::chrono::system_clock::now();
	std::thread::id                                    thread_id  = std::this_thread::get_id();
};

/**
 * @brief Build a record stamped with the calling thread and the current time.
 * Messages longer than @c log_defaults::MAX_MESSAGE_LENGTH are truncated.
 */
inline LogRecord make_record(const LogLevel level, const std::string_view message, const std::source_location loc) {
	LogRecord record{};
	record.level          = level;
	record.message_length = std::min(message.length(), log_defaults::MAX_MESSAGE_LENGTH);
	record.location       = loc;
	std::memcpy(record.message, message.data(), record.message_length);
	return record;
}

/**
 * @brief Configuration for @c AsyncLogger
 */
struct LogConfig {
	// Enable writes to the log file. When false, the log file is not opened and file output is fully skipped.
	bool enable_file = true;

	// Enable writes to stdout
	bool enable_cout = false;

	// Minimum severity written to the log file. A record is written if it is
	// at least as severe as this threshold (numerically <=, since lower numeric levels are more severe).
	LogLevel min_log_file_level = LogLevel::e_INFO;
	// Minimum severity written to stdout. Same comparison as the file threshold.
	LogLevel min_cout_level = LogLevel::e_ERROR;

	// TODO: Support log file rolling.
	std::filesystem::path log_file = "./logs/async_logger.log";

	/**
	 * Format String using % placeholders.
	 * @p %T - Timestamp
	 * @p %t - Thread ID
	 * @p %L - Log Level
	 * @p %f - File Name
	 * @p %l - Line Number
	 * @p %F - Function Name
	 * @p %m - Log Message
	 */
	std::string format = "%T [%L] %f:%l (%F) %m";

	BackPressurePolicy back_pressure_policy = BackPressurePolicy::e_DROP_BELOW_LEVEL;

	/**
	 * Threshold used by @c e_DROP_BELOW_LEVEL policy.
	 * Messages with severity below this level are dropped when the queue is full.
	 * Messages at or above this level block until space is available.
	 */
	LogLevel drop_threshold = LogLevel::e_WARN;
};
}

#endif  // DSL_DALL_LOG_RECORD_H_
