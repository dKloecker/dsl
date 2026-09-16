//
// Created by Dominic Kloecker on 16/09/2026.
//
// MPMC-specific behaviour: the sequence-number slot protocol (a slot is only
// reusable one lap later), clear(), and the multi-producer/multi-consumer
// guarantee that every element is handed to exactly one consumer.
//
#include <algorithm>
#include <atomic>
#include <barrier>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include "dclc_bounded_queue_test_types.h"
#include "dclc_mpmc_bounded_queue.h"

namespace dcl::test {

// -- Slot sequencing ---------------------------------------------------------

TEST(MpmcQueue, SlotIsOnlyReusableAfterItHasBeenConsumed) {
    dcl::mpmc_bounded_queue<int, 2> q;
    ASSERT_TRUE(q.push(1));
    ASSERT_TRUE(q.push(2));
    ASSERT_FALSE(q.push(3)) << "slot 0 is still occupied";

    int val = -1;
    ASSERT_TRUE(q.pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(q.push(3)) << "slot 0 must be reusable once consumed";
    EXPECT_FALSE(q.push(4));

    ASSERT_TRUE(q.pop(val));
    EXPECT_EQ(val, 2);
    ASSERT_TRUE(q.pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_TRUE(q.empty());
}

TEST(MpmcQueue, SequenceNumbersStayConsistentOverManyLaps) {
    dcl::mpmc_bounded_queue<int, 2> q;

    // 1000 laps over a 2-slot buffer: any sequence drift shows up as a spurious
    // full or empty result.
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(q.push(i)) << "lap " << i;
        int val = -1;
        ASSERT_TRUE(q.pop(val)) << "lap " << i;
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(q.empty());
}

TEST(MpmcQueue, MinimumCapacityIsTwo) {
    EXPECT_EQ((dcl::mpmc_bounded_queue<int, 2>::capacity), size_t{2});

    dcl::mpmc_bounded_queue<int, 2> q;
    ASSERT_TRUE(q.push(1));
    ASSERT_TRUE(q.push(2));
    EXPECT_FALSE(q.push(3));
}

// -- clear -------------------------------------------------------------------

TEST(MpmcQueue, ClearEmptiesTheQueue) {
    dcl::mpmc_bounded_queue<int, 8> q;
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(q.push(i));

    q.clear();

    EXPECT_TRUE(q.empty());
    int val = -1;
    EXPECT_FALSE(q.pop(val));
}

TEST(MpmcQueue, ClearDestroysRemainingElements) {
    Counters c;
    {
        dcl::mpmc_bounded_queue<Tracked, 8> q;
        for (int i = 0; i < 5; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q.push(source));
        }
        ASSERT_EQ(c.live(), 5);

        q.clear();
        EXPECT_EQ(c.live(), 0) << "clear must destroy the elements it drops";
    }
    EXPECT_EQ(c.live(), 0);
}

TEST(MpmcQueue, ClearOnAnEmptyQueueIsANoOp) {
    Counters c;
    {
        dcl::mpmc_bounded_queue<Tracked, 8> q;
        q.clear();
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(c.constructed, 0);
        EXPECT_EQ(c.destroyed, 0);
    }
    EXPECT_EQ(c.live(), 0);
}

TEST(MpmcQueue, ClearReleasesEverySlotForReuse) {
    dcl::mpmc_bounded_queue<int, 4> q;
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(i));

    q.clear();

    for (int i = 100; i < 104; ++i) ASSERT_TRUE(q.push(i)) << "full capacity must be available again";
    EXPECT_FALSE(q.push(999));
    for (int i = 100; i < 104; ++i) {
        int val = -1;
        ASSERT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST(MpmcQueue, ClearHandlesAWrappedRange) {
    Counters c;
    {
        dcl::mpmc_bounded_queue<Tracked, 4> q;
        Tracked                             sink{&c, -1};

        for (int i = 0; i < 4; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q.push(source));
        }
        for (int i = 0; i < 3; ++i) ASSERT_TRUE(q.pop(sink));
        for (int i = 4; i < 7; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q.push(source));
        }
        ASSERT_EQ(c.live(), 5) << "four queued elements plus the sink";

        q.clear();
        EXPECT_EQ(c.live(), 1) << "only the sink should remain";
        EXPECT_TRUE(q.empty());

        // The released slots must still be usable.
        for (int i = 0; i < 4; ++i) {
            Tracked source{&c, i};
            EXPECT_TRUE(q.push(source));
        }
    }
    EXPECT_EQ(c.live(), 0);
}

// -- Concurrency -------------------------------------------------------------

TEST(MpmcQueue, EveryElementIsDeliveredToExactlyOneConsumer) {
    constexpr int                    PRODUCERS  = 4;
    constexpr int                    CONSUMERS  = 4;
    constexpr int                    PER_THREAD = 20000;
    constexpr int                    TOTAL      = PRODUCERS * PER_THREAD;
    dcl::mpmc_bounded_queue<int, 64> q;

    std::atomic<int>         consumed{0};
    std::barrier             sync{PRODUCERS + CONSUMERS};
    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + CONSUMERS);

    for (int p = 0; p < PRODUCERS; ++p) {
        threads.emplace_back([&, p] {
            sync.arrive_and_wait();
            for (int i = 0; i < PER_THREAD; ++i)
                while (!q.push(p * PER_THREAD + i)) std::this_thread::yield();
        });
    }

    // Each consumer collects into its own vector; the union is checked at the end.
    std::vector<std::vector<int> > received(CONSUMERS);
    for (int cidx = 0; cidx < CONSUMERS; ++cidx) {
        threads.emplace_back([&, cidx] {
            sync.arrive_and_wait();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            int        val      = 0;
            while (consumed.load(std::memory_order_relaxed) < TOTAL) {
                if (q.pop(val)) {
                    received[cidx].push_back(val);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else if (std::chrono::steady_clock::now() > deadline) break;
            }
        });
    }

    for (auto &t: threads) t.join();

    std::vector<int> all;
    all.reserve(TOTAL);
    for (const auto &per_consumer: received) all.insert(all.end(), per_consumer.begin(), per_consumer.end());

    ASSERT_EQ(all.size(), static_cast<size_t>(TOTAL)) << "elements were lost, duplicated, or the run timed out";
    std::ranges::sort(all);
    for (int i = 0; i < TOTAL; ++i) ASSERT_EQ(all[i], i) << "value " << i << " missing or delivered twice";
    EXPECT_TRUE(q.empty());
}

TEST(MpmcQueue, EachProducersElementsStayInOrderWithinAConsumer) {
    // Consumers race each other, so global FIFO is not promised -- but a single
    // consumer must never see one producer's elements out of order.
    constexpr int                    PRODUCERS  = 4;
    constexpr int                    CONSUMERS  = 2;
    constexpr int                    PER_THREAD = 10000;
    constexpr int                    TOTAL      = PRODUCERS * PER_THREAD;
    dcl::mpmc_bounded_queue<int, 16> q;

    std::atomic<int>              consumed{0};
    std::vector<std::vector<int>> received(CONSUMERS);
    std::barrier                  sync{PRODUCERS + CONSUMERS};
    std::vector<std::thread>      threads;
    threads.reserve(PRODUCERS + CONSUMERS);

    for (int p = 0; p < PRODUCERS; ++p) {
        threads.emplace_back([&, p] {
            sync.arrive_and_wait();
            for (int i = 0; i < PER_THREAD; ++i)
                while (!q.push(p * PER_THREAD + i)) std::this_thread::yield();
        });
    }
    for (int cidx = 0; cidx < CONSUMERS; ++cidx) {
        threads.emplace_back([&, cidx] {
            sync.arrive_and_wait();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            int        val      = 0;
            while (consumed.load(std::memory_order_relaxed) < TOTAL) {
                if (q.pop(val)) {
                    received[cidx].push_back(val);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else if (std::chrono::steady_clock::now() > deadline) break;
            }
        });
    }
    for (auto &t: threads) t.join();

    ASSERT_EQ(consumed.load(), TOTAL);
    for (int cidx = 0; cidx < CONSUMERS; ++cidx) {
        std::vector<int> last(PRODUCERS, -1);
        for (const int val: received[cidx]) {
            const int producer = val / PER_THREAD;
            const int seq      = val % PER_THREAD;
            EXPECT_GT(seq, last[producer])
                << "consumer " << cidx << " saw producer " << producer << " out of order";
            last[producer] = seq;
        }
    }
}

TEST(MpmcQueue, ConcurrentPushesAreNotTorn) {
    constexpr int                       PRODUCERS  = 4;
    constexpr int                       CONSUMERS  = 4;
    constexpr int                       PER_THREAD = 10000;
    constexpr int                       TOTAL      = PRODUCERS * PER_THREAD;
    dcl::mpmc_bounded_queue<Paired, 16> q;

    std::atomic<int>         consumed{0};
    std::atomic<int>         torn{0};
    std::barrier             sync{PRODUCERS + CONSUMERS};
    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + CONSUMERS);

    for (int p = 0; p < PRODUCERS; ++p) {
        threads.emplace_back([&, p] {
            sync.arrive_and_wait();
            for (int i = 0; i < PER_THREAD; ++i) {
                const auto value = static_cast<std::uint64_t>(p) * PER_THREAD + i;
                while (!q.push(Paired{value})) std::this_thread::yield();
            }
        });
    }
    for (int c = 0; c < CONSUMERS; ++c) {
        threads.emplace_back([&] {
            sync.arrive_and_wait();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            Paired     out;
            while (consumed.load(std::memory_order_relaxed) < TOTAL) {
                if (q.pop(out)) {
                    if (!out.consistent()) torn.fetch_add(1, std::memory_order_relaxed);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else if (std::chrono::steady_clock::now() > deadline) break;
            }
        });
    }
    for (auto &t: threads) t.join();

    EXPECT_EQ(consumed.load(), TOTAL);
    EXPECT_EQ(torn.load(), 0) << "a consumer observed a partially written element";
}

TEST(MpmcQueue, SurvivesHeavyContentionOnATinyBuffer) {
    // Capacity 2 with eight threads: producers spend most of their time being
    // rejected and consumers most of theirs finding the queue empty.
    constexpr int                   PRODUCERS  = 4;
    constexpr int                   CONSUMERS  = 4;
    constexpr int                   PER_THREAD = 5000;
    constexpr int                   TOTAL      = PRODUCERS * PER_THREAD;
    dcl::mpmc_bounded_queue<int, 2> q;

    std::atomic<int>         consumed{0};
    std::atomic<long long>   checksum{0};
    std::barrier             sync{PRODUCERS + CONSUMERS};
    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + CONSUMERS);

    long long expected = 0;
    for (int p = 0; p < PRODUCERS; ++p)
        for (int i = 0; i < PER_THREAD; ++i) expected += p * PER_THREAD + i;

    for (int p = 0; p < PRODUCERS; ++p) {
        threads.emplace_back([&, p] {
            sync.arrive_and_wait();
            for (int i = 0; i < PER_THREAD; ++i)
                while (!q.push(p * PER_THREAD + i)) std::this_thread::yield();
        });
    }
    for (int c = 0; c < CONSUMERS; ++c) {
        threads.emplace_back([&] {
            sync.arrive_and_wait();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            long long  local    = 0;
            int        val      = 0;
            while (consumed.load(std::memory_order_relaxed) < TOTAL) {
                if (q.pop(val)) {
                    local += val;
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else if (std::chrono::steady_clock::now() > deadline) break;
            }
            checksum.fetch_add(local, std::memory_order_relaxed);
        });
    }
    for (auto &t: threads) t.join();

    EXPECT_EQ(consumed.load(), TOTAL);
    EXPECT_EQ(checksum.load(), expected);
    EXPECT_TRUE(q.empty());
}

TEST(MpmcQueue, NonTrivialElementsSurviveContention) {
    constexpr int                            PRODUCERS  = 4;
    constexpr int                            CONSUMERS  = 4;
    constexpr int                            PER_THREAD = 2000;
    constexpr int                            TOTAL      = PRODUCERS * PER_THREAD;
    dcl::mpmc_bounded_queue<std::string, 32> q;

    std::atomic<int>         consumed{0};
    std::atomic<int>         corrupt{0};
    std::barrier             sync{PRODUCERS + CONSUMERS};
    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + CONSUMERS);

    for (int p = 0; p < PRODUCERS; ++p) {
        threads.emplace_back([&, p] {
            sync.arrive_and_wait();
            for (int i = 0; i < PER_THREAD; ++i) {
                const std::string payload = std::string(64, 'a' + p) + std::to_string(i);
                while (!q.push(payload)) std::this_thread::yield();
            }
        });
    }
    for (int c = 0; c < CONSUMERS; ++c) {
        threads.emplace_back([&] {
            sync.arrive_and_wait();
            const auto  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            std::string out;
            while (consumed.load(std::memory_order_relaxed) < TOTAL) {
                if (q.pop(out)) {
                    const std::string prefix(64, out.empty() ? '?' : out.front());
                    if (out.size() < 64 || out.compare(0, 64, prefix) != 0)
                        corrupt.fetch_add(1, std::memory_order_relaxed);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else if (std::chrono::steady_clock::now() > deadline) break;
            }
        });
    }
    for (auto &t: threads) t.join();

    EXPECT_EQ(consumed.load(), TOTAL);
    EXPECT_EQ(corrupt.load(), 0) << "a consumer observed a malformed string";
}
} // namespace dcl::test
