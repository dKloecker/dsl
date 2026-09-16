//
// Created by Dominic Kloecker on 23/03/2026.
//
#include <gtest/gtest.h>
#include "dclc_spsc_bounded_queue.h"

#include <atomic>
#include <thread>

#include "dclc_bounded_queue_test_types.h"

namespace dcl::test {
TEST(SpscQueue, TopReturnsNullptrWhenEmpty) {
    dcl::spsc_bounded_queue < int, 16 > q;
    EXPECT_EQ(q.top(), nullptr);
}

TEST(SpscQueue, TryPopReturnsFalseWhenEmpty) {
    dcl::spsc_bounded_queue < int, 16 > q;
    int val = 42;
    EXPECT_FALSE(q.pop(val));
    EXPECT_EQ(val, 42);
}

TEST(SpscQueue, PopReturnsNulloptWhenEmpty) {
    dcl::spsc_bounded_queue < int, 16 > q;
    EXPECT_EQ(q.try_pop(), std::nullopt);
}

TEST(SpscQueue, PushMakesTopAvailable) {
    dcl::spsc_bounded_queue < int, 16 > q;
    q.push(1);
    ASSERT_NE(q.top(), nullptr);
    EXPECT_EQ(*q.top(), 1);
}

TEST(SpscQueue, PushDoesNotAdvanceTop) {
    dcl::spsc_bounded_queue < int, 16 > q;
    q.push(1);
    q.push(2);
    ASSERT_NE(q.top(), nullptr);
    EXPECT_EQ(*q.top(), 1);
}

TEST(SpscQueue, PushFailsWhenFull) {
    dcl::spsc_bounded_queue < int, 16 > q;
    for (int i = 0; i < 16; i++) ASSERT_TRUE(q.push(i));
    EXPECT_FALSE(q.push(999));
}

TEST(SpscQueue, TryPopReturnsOldestElement) {
    dcl::spsc_bounded_queue < int, 16 > q;
    q.push(1);
    q.push(2);
    int val = 0;
    ASSERT_TRUE(q.pop(val));
    EXPECT_EQ(val, 1);
}

TEST(SpscQueue, PopReturnsOldestElement) {
    dcl::spsc_bounded_queue < int, 16 > q;
    q.push(1);
    q.push(2);
    EXPECT_EQ(q.try_pop(), 1);
}

TEST(SpscQueue, FifoOrdering) {
    dcl::spsc_bounded_queue < int, 16 > q;
    for (int i = 0; i < 16; i++) q.push(i);

    for (int i = 0; i < 16; i++) {
        ASSERT_NE(q.top(), nullptr);
        EXPECT_EQ(*q.top(), i);
        EXPECT_EQ(q.try_pop(), i);
    }
    EXPECT_EQ(q.top(), nullptr);
}

TEST(SpscQueue, PushPopAcrossWrapBoundary) {
    dcl::spsc_bounded_queue < int, 4 > q;
    // Fill and drain twice to force the indices past capacity
    for (int round = 0; round < 3; round++) {
        // fill
        for (int i = 0; i < 4; i++) ASSERT_TRUE(q.push(round * 4 + i));
        // drain
        for (int i = 0; i < 4; i++) EXPECT_EQ(q.try_pop(), round * 4 + i);
    }
}

TEST(SpscQueue, DestructorDestroysRemainingElements) {
    // Counting destructions alone cannot distinguish the queue tearing its
    // elements down from the pushed temporaries expiring, so track how many
    // Tracked objects are alive at each point instead.
    Counters c;
    {
        dcl::spsc_bounded_queue<Tracked, 8> q;
        for (int i = 0; i < 8; i++) {
            Tracked source{&c, i};
            ASSERT_TRUE(q.push(source));
        }
        ASSERT_EQ(c.live(), 8) << "eight copies should be sitting in the queue";
    }
    EXPECT_EQ(c.live(), 0);
}

TEST(SpscQueue, ProducerConsumerThreads) {
    dcl::spsc_bounded_queue<std::string, 16> q;
    std::vector<std::string>         elements{"Hello", "World", "How", "Are", "You", "Today", "My", "Friend"};

    std::vector<std::string> popped;
    std::atomic_bool         complete = false;

    std::thread t1{
        [&] {
            for (const auto &el: elements) {
                q.push(el);
            }
            complete.store(true);
        }
    };
    std::thread t2{
        [&] {
            std::string pop;
            while (!complete.load(std::memory_order_acquire)) {
                if (q.pop(pop)) popped.push_back(pop);
            }
            // Drain anything remaining
            while (q.pop(pop)) popped.push_back(pop);
        }
    };
    t1.join();
    t2.join();
    EXPECT_EQ(elements, popped);
    EXPECT_TRUE(q.empty());
}

// -- reset -------------------------------------------------------------------

TEST(SpscQueue, ResetEmptiesTheQueue) {
    dcl::spsc_bounded_queue<int, 8> q;
    for (int i = 0; i < 5; i++) ASSERT_TRUE(q.push(i));

    q.reset();

    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.top(), nullptr);
    int val = -1;
    EXPECT_FALSE(q.pop(val));
}

TEST(SpscQueue, ResetDestroysRemainingElements) {
    Counters c;
    {
        dcl::spsc_bounded_queue<Tracked, 8> q;
        for (int i = 0; i < 5; i++) {
            Tracked source{&c, i};
            ASSERT_TRUE(q.push(source));
        }
        ASSERT_EQ(c.live(), 5);

        q.reset();
        EXPECT_EQ(c.live(), 0) << "reset must destroy the elements it drops";
    }
    EXPECT_EQ(c.live(), 0);
}

TEST(SpscQueue, ResetOnAnEmptyQueueIsANoOp) {
    Counters c;
    {
        dcl::spsc_bounded_queue<Tracked, 8> q;
        q.reset();
        EXPECT_TRUE(q.empty());
        EXPECT_EQ(c.constructed, 0);
        EXPECT_EQ(c.destroyed, 0);
    }
    EXPECT_EQ(c.live(), 0);
}

TEST(SpscQueue, QueueIsReusableAfterReset) {
    dcl::spsc_bounded_queue<int, 4> q;
    for (int i = 0; i < 4; i++) ASSERT_TRUE(q.push(i));

    q.reset();

    for (int i = 100; i < 104; i++) ASSERT_TRUE(q.push(i)) << "full capacity must be available again";
    EXPECT_FALSE(q.push(999));
    for (int i = 100; i < 104; i++) {
        int val = -1;
        ASSERT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
}

// -- try_pop / top -----------------------------------------------------------

TEST(SpscQueue, TryPopDestroysTheStoredElement) {
    Counters c;
    {
        dcl::spsc_bounded_queue<Tracked, 8> q;
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

TEST(SpscQueue, TryPopFreesASlot) {
    dcl::spsc_bounded_queue<int, 4> q;
    for (int i = 0; i < 4; i++) ASSERT_TRUE(q.push(i));
    ASSERT_FALSE(q.push(99));

    EXPECT_EQ(q.try_pop(), 0);
    EXPECT_TRUE(q.push(99));
    EXPECT_FALSE(q.push(100));
}

TEST(SpscQueue, TopFollowsThePopsAcrossTheWrapBoundary) {
    dcl::spsc_bounded_queue<int, 4> q;

    for (int lap = 0; lap < 5; lap++) {
        for (int i = 0; i < 4; i++) ASSERT_TRUE(q.push(lap * 4 + i));
        for (int i = 0; i < 4; i++) {
            ASSERT_NE(q.top(), nullptr);
            EXPECT_EQ(*q.top(), lap * 4 + i);
            int val = -1;
            ASSERT_TRUE(q.pop(val));
        }
        EXPECT_EQ(q.top(), nullptr);
    }
}

// -- Concurrency -------------------------------------------------------------

TEST(SpscQueue, ProducerAndConsumerAgreeOnOrderUnderLoad) {
    // Capacity 16 against 200k elements keeps both the full and the empty path
    // hot for the whole run.
    constexpr int                    TOTAL = 200000;
    dcl::spsc_bounded_queue<int, 16> q;

    std::atomic<int> received{0};
    std::atomic<int> out_of_order{0};

    std::thread producer([&] {
        for (int i = 0; i < TOTAL; i++)
            while (!q.push(i)) std::this_thread::yield();
    });

    std::thread consumer([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        int        expected = 0;
        int        val      = 0;
        while (expected < TOTAL) {
            if (q.pop(val)) {
                if (val != expected) out_of_order.fetch_add(1, std::memory_order_relaxed);
                ++expected;
                received.store(expected, std::memory_order_relaxed);
            } else if (std::chrono::steady_clock::now() > deadline) break;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received.load(), TOTAL) << "elements were lost or the consumer timed out";
    EXPECT_EQ(out_of_order.load(), 0);
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueue, NonTrivialElementsSurviveTheHandover) {
    constexpr int                            TOTAL = 20000;
    dcl::spsc_bounded_queue<std::string, 16> q;

    std::atomic<int> mismatches{0};
    std::atomic<int> received{0};

    std::thread producer([&] {
        for (int i = 0; i < TOTAL; i++) {
            const std::string payload = std::string(64, 'x') + std::to_string(i);
            while (!q.push(payload)) std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        const auto  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        std::string out;
        int         expected = 0;
        while (expected < TOTAL) {
            if (q.pop(out)) {
                if (out != std::string(64, 'x') + std::to_string(expected))
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                ++expected;
                received.store(expected, std::memory_order_relaxed);
            } else if (std::chrono::steady_clock::now() > deadline) break;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received.load(), TOTAL);
    EXPECT_EQ(mismatches.load(), 0);
}
} // namespace dcl::test
