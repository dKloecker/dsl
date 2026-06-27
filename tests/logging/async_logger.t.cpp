//
// Created by Dominic Kloecker on 03/04/2026.
//

#include <filesystem>
#include <gtest/gtest.h>
#include <barrier>

#include "dsl/logging/dsl_async_logger.h"
#include "dsl/logging/dsl_logger_enums.h"

namespace dsl::test::logging {
namespace fs = std::filesystem;

class AsyncLoggerTest : public ::testing::Test {
protected:
    fs::path log_path;

    void SetUp() override {
        const auto *info = testing::UnitTest::GetInstance()->current_test_info();
        log_path         = std::string("./test_async_logger_") + info->name() + ".txt";
        if (fs::exists(log_path)) fs::remove(log_path);
        // Ensure clean state before each test
        AsyncLogger<>::instance().reset();
    }

    void TearDown() override {
        AsyncLogger<>::instance().reset();
        if (fs::exists(log_path)) {
            fs::remove(log_path);
        }
    }
};

TEST_F(AsyncLoggerTest, CreatesLogFile) {
    LogConfig cfg{.min_log_file_level = LogLevel::e_DEBUG, .log_file = log_path};

    AsyncLogger<>::instance().init(cfg);
    EXPECT_TRUE(fs::exists(log_path));
}

TEST_F(AsyncLoggerTest, CreatesParentDirectories) {
    log_path = "./logs/nested/test.txt";

    const LogConfig cfg{.min_log_file_level = LogLevel::e_DEBUG, .log_file = log_path};
    AsyncLogger<>::instance().init(cfg);
    EXPECT_TRUE(fs::exists(log_path));
    fs::remove_all("./logs");
}

TEST_F(AsyncLoggerTest, LogsWrittenToFile) {
    const LogConfig cfg{.min_log_file_level = LogLevel::e_DEBUG, .log_file = log_path, .format = "%L %m"};

    AsyncLogger<>::instance().init(cfg);
    LOG_DEBUG("Some DEBUG Message");
    LOG_INFO("Some INFO Message");
    LOG_WARN("Some WARNING Message");
    LOG_ERROR("Some ERROR Message");
    LOG_FATAL("Some FATAL Message");

    // Flush all records to disk
    AsyncLogger<>::instance().reset();

    std::ifstream            log_file(log_path);
    std::vector<std::string> lines;
    std::string              line;
    while (std::getline(log_file, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 5);
    EXPECT_EQ(lines[0], "DEBUG Some DEBUG Message");
    EXPECT_EQ(lines[1], "INFO Some INFO Message");
    EXPECT_EQ(lines[2], "WARN Some WARNING Message");
    EXPECT_EQ(lines[3], "ERROR Some ERROR Message");
    EXPECT_EQ(lines[4], "FATAL Some FATAL Message");
}

TEST_F(AsyncLoggerTest, LogsAreTruncatedIfTooLarge) {
    const LogConfig cfg{.min_log_file_level = LogLevel::e_DEBUG, .log_file = log_path, .format = "%m"};

    // Build a message longer than max message length
    std::string long_message(Logger::MAX_MESSAGE_LENGTH + 100, 'X');

    AsyncLogger<>::instance().init(cfg);
    AsyncLogger<>::instance().info(long_message);
    AsyncLogger<>::instance().reset();

    std::ifstream log_file(log_path);
    std::string   line;
    std::getline(log_file, line);

    EXPECT_EQ(line.length(), Logger::MAX_MESSAGE_LENGTH);
    EXPECT_EQ(line, std::string(Logger::MAX_MESSAGE_LENGTH, 'X'));
}

TEST_F(AsyncLoggerTest, LogsWrittenAfterBufferFlush) {
    const LogConfig cfg{.min_log_file_level = LogLevel::e_DEBUG, .log_file = log_path, .format = "%m"};

    AsyncLogger<>::instance().init(cfg);

    // Write enough records to exceed the stream buffer
    constexpr int num_records = 500;
    for (int i = 0; i < num_records; ++i) {
        AsyncLogger<>::instance().info("Filler message to exceed stream buffer capacity");
    }

    AsyncLogger<>::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    EXPECT_EQ(line_count, num_records);
}

TEST_F(AsyncLoggerTest, LogsBelowMinLevelDropped) {
    const LogConfig cfg{.min_log_file_level = LogLevel::e_WARN, .log_file = log_path, .format = "%L %m"};

    AsyncLogger<>::instance().init(cfg);
    LOG_DEBUG("should be dropped");
    LOG_INFO("should also be dropped");
    LOG_WARN("should appear");
    LOG_ERROR("should also appear");
    AsyncLogger<>::instance().reset();

    std::ifstream            log_file(log_path);
    std::vector<std::string> lines;
    std::string              line;
    while (std::getline(log_file, line)) {
        lines.push_back(line);
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "WARN should appear");
    EXPECT_EQ(lines[1], "ERROR should also appear");
}

TEST_F(AsyncLoggerTest, DropQueuePolicyDrops) {
    using SmallLogger = AsyncLogger<4, 32>;
    const LogConfig cfg{
        .min_log_file_level   = LogLevel::e_DEBUG,
        .log_file             = log_path,
        .format               = "%m",
        .back_pressure_policy = BackPressurePolicy::e_DROP
    };

    SmallLogger::instance().init(cfg);

    // Flood the queue with messages (some will be dropped)
    for (int i = 0; i < 100; ++i) {
        SmallLogger::instance().info("flood message");
    }

    SmallLogger::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    // Some messages should have been dropped
    EXPECT_LT(line_count, 100);
    EXPECT_GT(line_count, 0);
}

TEST_F(AsyncLoggerTest, DropBelowLevelDrops) {
    using SmallLogger = AsyncLogger<4, 32>;

    const LogConfig cfg{
        .min_log_file_level   = LogLevel::e_DEBUG,
        .log_file             = log_path,
        .format               = "%L %m",
        .back_pressure_policy = BackPressurePolicy::e_DROP_BELOW_LEVEL,
        .drop_threshold       = LogLevel::e_WARN
    };

    SmallLogger::instance().init(cfg);

    for (int i = 0; i < 100; ++i) {
        SmallLogger::instance().debug("low priority flood");
    }

    SmallLogger::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    EXPECT_LT(line_count, 100);
}

TEST_F(AsyncLoggerTest, DropBelowLevelLogsAboveThreshold) {
    using SmallLogger = AsyncLogger<4, 32>;

    const LogConfig cfg{
        .min_log_file_level   = LogLevel::e_DEBUG,
        .log_file             = log_path,
        .format               = "%L %m",
        .back_pressure_policy = BackPressurePolicy::e_DROP_BELOW_LEVEL,
        .drop_threshold       = LogLevel::e_WARN
    };

    SmallLogger::instance().init(cfg);

    // High Priority should not have been blocked
    for (int i = 0; i < 20; ++i) {
        SmallLogger::instance().error("critical message");
    }

    SmallLogger::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    EXPECT_EQ(line_count, 20);
}

TEST_F(AsyncLoggerTest, BlockPolicyBlocksUntilProcessed) {
    using SmallLogger = AsyncLogger<4, 32>;

    const LogConfig cfg{
        .min_log_file_level   = LogLevel::e_DEBUG,
        .log_file             = log_path,
        .format               = "%m",
        .back_pressure_policy = BackPressurePolicy::e_BLOCK
    };

    SmallLogger::instance().init(cfg);

    // With block policy, all messages must eventually be written
    constexpr int num_records = 100;
    for (int i = 0; i < num_records; ++i) {
        SmallLogger::instance().info("blocking message");
    }

    SmallLogger::instance().reset();

    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    while (std::getline(log_file, line)) {
        ++line_count;
    }

    EXPECT_EQ(line_count, num_records);
}

TEST_F(AsyncLoggerTest, MultipleThreadsLoggingAtTheSameTime) {
    const LogConfig cfg{
       .min_log_file_level   = LogLevel::e_DEBUG,
       .log_file             = log_path,
       .format               = "%t", // Expecting thread ID
       .back_pressure_policy = BackPressurePolicy::e_BLOCK,
       .drop_threshold       = LogLevel::e_DEBUG
    };

    AsyncLogger<>::instance().init(cfg);

    const size_t num_threads = 10;
    const size_t msgs_per_thread = 100;

    std::mutex mtx;
    std::barrier sync(num_threads);
    std::vector<std::jthread> threads;
    std::set<std::thread::id> ids;

    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; i++) {
       threads.emplace_back([&]() {
          {
             // Record thread ID
             std::scoped_lock<std::mutex> lck{mtx};
             ids.insert(std::this_thread::get_id());
          }

          // Wait until all threads are spun up and recorded
          sync.arrive_and_wait();
       	  // Spam logger
          for (int j = 0; j < msgs_per_thread; ++j) {
                 AsyncLogger<>::instance().info("message");
          }
       });
    }

	// Wait for all threads to finish logging before the shutdown is done.
    threads.clear();
    AsyncLogger<>::instance().reset();

    // Verify the log file contents
    std::ifstream log_file(log_path);
    int           line_count = 0;
    std::string   line;
    std::set<std::string> logged_thread_ids;

    while (std::getline(log_file, line)) {
       ++line_count;
       // Assuming "%t" formats the line to be exactly the thread ID string,
       // or at least contains it. We collect them to ensure every thread was represented.
       logged_thread_ids.insert(line);
    }
    EXPECT_EQ(line_count, num_threads * msgs_per_thread);
    EXPECT_EQ(logged_thread_ids.size(), num_threads);

	// Ensure the ids were captured correctly
	for (const auto& id : ids) {
		std::stringstream ss;
		ss << id;
		EXPECT_TRUE(logged_thread_ids.contains(ss.str()));
	}
}

}
