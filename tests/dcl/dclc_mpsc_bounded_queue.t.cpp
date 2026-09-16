//
// Created by Dominic Kloecker on 16/09/2026.
//
// MPSC-specific behaviour: the try_pop/top/reset surface the shared suite does
// not cover, and the multi-producer guarantees -- no lost or duplicated element,
// no torn write, and FIFO order preserved per producer.
//
#include <algorithm>
#include <atomic>
#include <barrier>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include "dclc_bounded_queue_test_types.h"
#include "dclc_mpsc_bounded_queue.h"

namespace dcl::test {

// -- top ---------------------------------------------------------------------

TEST(MpscQueue, TopReturnsNullptrWhenEmpty) {
    dcl::mpsc_bounded_queue<int, 16> q;
    EXPECT_EQ(q.top(), nullptr);
}

TEST(MpscQueue, TopReturnsTheOldestElement) {
    dcl::mpsc_bounded_queue<int, 16> q;
    ASSERT_TRUE(q.push(1));
    ASSERT_TRUE(q.push(2));

    ASSERT_NE(q.top(), nullptr);
    EXPECT_EQ(*q.top(), 1);
}

TEST(MpscQueue, TopDoesNotConsume) {
    dcl::mpsc_bounded_queue<int, 16> q;
    ASSERT_TRUE(q.push(1));

    ASSERT_NE(q.top(), nullptr);
    EXPECT_EQ(*q.top(), 1);
    EXPECT_EQ(*q.top(), 1);

    int val = -1;
    ASSERT_TRUE(q.pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_EQ(q.top(), nullptr);
}

TEST(MpscQueue, TopFollowsThePopsAcrossTheWrapBoundary) {
    dcl::mpsc_bounded_queue<int, 4> q;

    for (int lap = 0; lap < 5; ++lap) {
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(lap * 4 + i));
        for (int i = 0; i < 4; ++i) {
            ASSERT_NE(q.top(), nullptr);
            EXPECT_EQ(*q.top(), lap * 4 + i);
            int val = -1;
            ASSERT_TRUE(q.pop(val));
        }
        EXPECT_EQ(q.top(), nullptr);
    }
}

// -- try_pop -----------------------------------------------------------------

TEST(MpscQueue, TryPopReturnsNulloptWhenEmpty) {
    dcl::mpsc_bounded_queue<int, 16> q;
    EXPECT_EQ(q.try_pop(), std::nullopt);
}

TEST(MpscQueue, TryPopReturnsElementsInFifoOrder) {
    dcl::mpsc_bounded_queue<int, 16> q;
    for (int i = 0; i < 16; ++i) ASSERT_TRUE(q.push(i));

    for (int i = 0; i < 16; ++i) EXPECT_EQ(q.try_pop(), i);
    EXPECT_EQ(q.try_pop(), std::nullopt);
}

TEST(MpscQueue, TryPopDestroysTheStoredElement) {
    Counters c;
    {
        dcl::mpsc_bounded_queue<Tracked, 8> q;
        {
            Tracked source{&c, 3};
            ASSERT_TRUE(q.push(source));
        }
        ASSERT_EQ(c.live(), 1);

        auto popped = q.try_pop();
        ASSERT_TRUE(popped.has_value());
        EXPECT_EQ(popped->value, 3);
        EXPECT_EQ(c.live(), 1) << "only the returned element should remain alive";
    }
    EXPECT_EQ(c.live(), 0);
}

TEST(MpscQueue, TryPopFreesASlot) {
    dcl::mpsc_bounded_queue<int, 4> q;
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(i));
    ASSERT_FALSE(q.push(99));

    EXPECT_EQ(q.try_pop(), 0);
    EXPECT_TRUE(q.push(99));
    EXPECT_FALSE(q.push(100));
}

// -- reset -------------------------------------------------------------------

TEST(MpscQueue, ResetEmptiesTheQueue) {
    dcl::mpsc_bounded_queue<int, 8> q;
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(q.push(i));

    q.reset();

    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.top(), nullptr);
    int val = -1;
    EXPECT_FALSE(q.pop(val));
}

TEST(MpscQueue, ResetDestroysRemainingElements) {
    Counters c;
    {
        dcl::mpsc_bounded_queue<Tracked, 8> q;
        for (int i = 0; i < 5; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q.push(source));
        }
        ASSERT_EQ(c.live(), 5);

        q.reset();
        EXPECT_EQ(c.live(), 0) << "reset must destroy the elements it drops";
    }
    EXPECT_EQ(c.live(), 0);
}

TEST(MpscQueue, ResetOnAnEmptyQueueIsANoOp) {
    Counters c;
    {
        dcl::mpsc_bounded_queue<Tracked, 8> q;
        q.reset();
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(c.constructed, 0);
        EXPECT_EQ(c.destroyed, 0);
    }
    EXPECT_EQ(c.live(), 0);
}

TEST(MpscQueue, QueueIsReusableAfterReset) {
    dcl::mpsc_bounded_queue<int, 4> q;
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(i));

    q.reset();

    for (int i = 100; i < 104; ++i) ASSERT_TRUE(q.push(i)) << "full capacity must be available again";
    EXPECT_FALSE(q.push(999));
    for (int i = 100; i < 104; ++i) {
        int val = -1;
        ASSERT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
}

// -- Concurrency -------------------------------------------------------------

TEST(MpscQueue, ManyProducersLoseNoElements) {
    constexpr int                            PRODUCERS = 4;
    constexpr int                            PER_THREAD = 20000;
    constexpr int                            TOTAL     = PRODUCERS * PER_THREAD;
    dcl::mpsc_bounded_queue<int, 64>         q;

    std::vector<int>    received;
    received.reserve(TOTAL);
    std::atomic<bool>   start{false};

    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&, p] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int i = 0; i < PER_THREAD; ++i) {
                // Value encodes producer and sequence so ordering stays checkable.
                while (!q.push(p * PER_THREAD + i)) std::this_thread::yield();
            }
        });
    }

    std::thread consumer([&] {
        start.store(true, std::memory_order_release);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        int        val      = 0;
        while (static_cast<int>(received.size()) < TOTAL) {
            if (q.pop(val)) received.push_back(val);
            else if (std::chrono::steady_clock::now() > deadline) break;
        }
    });

    for (auto &t: producers) t.join();
    consumer.join();

    ASSERT_EQ(received.size(), static_cast<size_t>(TOTAL)) << "elements were lost or the consumer timed out";

    // Every value pushed appears exactly once.
    std::vector<int> sorted = received;
    std::ranges::sort(sorted);
    for (int i = 0; i < TOTAL; ++i) ASSERT_EQ(sorted[i], i) << "value " << i << " missing or duplicated";

    // Each producer's own elements stay in the order it pushed them.
    std::vector<int> next(PRODUCERS, 0);
    for (const int val: received) {
        const int producer = val / PER_THREAD;
        EXPECT_EQ(val % PER_THREAD, next[producer]) << "producer " << producer << " observed out of order";
        ++next[producer];
    }
}

TEST(MpscQueue, ConcurrentPushesAreNotTorn) {
    constexpr int                       PRODUCERS  = 4;
    constexpr int                       PER_THREAD = 10000;
    constexpr int                       TOTAL      = PRODUCERS * PER_THREAD;
    dcl::mpsc_bounded_queue<Paired, 16> q;

    std::atomic<int>         consumed{0};
    std::atomic<bool>        producing{true};
    std::barrier             sync{PRODUCERS + 1};
    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);

    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&, p] {
            sync.arrive_and_wait();
            for (int i = 0; i < PER_THREAD; ++i) {
                const auto value = static_cast<std::uint64_t>(p) * PER_THREAD + i;
                while (!q.push(Paired{value})) std::this_thread::yield();
            }
        });
    }

    std::thread consumer([&] {
        sync.arrive_and_wait();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        Paired     out;
        while (consumed.load(std::memory_order_relaxed) < TOTAL) {
            if (q.pop(out)) {
                ASSERT_TRUE(out.consistent()) << "torn element: a=" << out.a << " b=" << out.b;
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else if (std::chrono::steady_clock::now() > deadline) break;
        }
        producing.store(false, std::memory_order_release);
    });

    for (auto &t: producers) t.join();
    consumer.join();
    EXPECT_EQ(consumed.load(), TOTAL);
}

TEST(MpscQueue, ProducersSeeFalseOnlyWhenTheQueueIsFull) {
    // A tiny queue with more producers than slots: every push either succeeds or
    // is rejected, and the total accepted must equal the total consumed.
    constexpr int                    PRODUCERS  = 8;
    constexpr int                    PER_THREAD = 5000;
    dcl::mpsc_bounded_queue<int, 2>  q;

    std::atomic<int>         accepted{0};
    std::atomic<bool>        done{false};
    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);

    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < PER_THREAD; ++i)
                if (q.push(i)) accepted.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::atomic<int> consumed{0};
    std::thread      consumer([&] {
        int val = 0;
        while (!done.load(std::memory_order_acquire)) {
            if (q.pop(val)) consumed.fetch_add(1, std::memory_order_relaxed);
        }
        while (q.pop(val)) consumed.fetch_add(1, std::memory_order_relaxed);
    });

    for (auto &t: producers) t.join();
    done.store(true, std::memory_order_release);
    consumer.join();

    EXPECT_EQ(consumed.load(), accepted.load()) << "accepted pushes and consumed elements disagree";
    EXPECT_TRUE(q.empty());
}

TEST(MpscQueue, NonTrivialElementsSurviveConcurrentProducers) {
    constexpr int                            PRODUCERS  = 4;
    constexpr int                            PER_THREAD = 2000;
    constexpr int                            TOTAL      = PRODUCERS * PER_THREAD;
    dcl::mpsc_bounded_queue<std::string, 32> q;

    std::vector<std::string> received;
    received.reserve(TOTAL);
    std::atomic<bool> done{false};

    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < PER_THREAD; ++i) {
                const std::string payload = std::string(64, 'a' + p) + std::to_string(i);
                while (!q.push(payload)) std::this_thread::yield();
            }
        });
    }

    std::thread consumer([&] {
        std::string out;
        while (!done.load(std::memory_order_acquire)) {
            if (q.pop(out)) received.push_back(out);
        }
        while (q.pop(out)) received.push_back(out);
    });

    for (auto &t: producers) t.join();
    done.store(true, std::memory_order_release);
    consumer.join();

    ASSERT_EQ(received.size(), static_cast<size_t>(TOTAL));
    std::vector<int> next(PRODUCERS, 0);
    for (const auto &s: received) {
        const int producer = s.front() - 'a';
        ASSERT_GE(producer, 0);
        ASSERT_LT(producer, PRODUCERS);
        EXPECT_EQ(s, std::string(64, 'a' + producer) + std::to_string(next[producer]));
        ++next[producer];
    }
}
} // namespace dcl::test
