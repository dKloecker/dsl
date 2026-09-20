//
// Created by Dominic Kloecker on 20/09/2026.

#include <atomic>
#include <chrono>
#include <deque>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include "dclc_bounded_queue_test_types.h"

namespace dcl::test {
template<typename Tag>
class BoundedQueue : public ::testing::Test {};

TYPED_TEST_SUITE(BoundedQueue, QueueTags, QueueTagNames);

TYPED_TEST(BoundedQueue, CapacityMatchesTheRequestedSize) {
    auto q = TypeParam::template make<int, 16>();
    EXPECT_EQ(q->capacity(), size_t{16});
}

TYPED_TEST(BoundedQueue, EveryRequestedSlotIsUsable) {
    auto q = TypeParam::template make<int, 8>();

    for (int i = 0; i < 8; ++i) ASSERT_TRUE(q->push(i)) << "push " << i << " of 8 rejected";
    EXPECT_FALSE(q->push(999));
}

TYPED_TEST(BoundedQueue, RuntimeSizingRejectsANonPowerOfTwoCapacity) {
    if constexpr (TypeParam::runtime_sized) {
        using Q = typename TypeParam::template queue<int, 16>;
        EXPECT_THROW((Q{5}), std::invalid_argument);
        EXPECT_THROW((Q{0}), std::invalid_argument);
    } else {
        GTEST_SKIP() << "compile-time sizing rejects this at compile time";
    }
}

TYPED_TEST(BoundedQueue, NewQueueIsEmpty) {
    auto q = TypeParam::template make<int, 16>();

    EXPECT_TRUE(q->empty());
    EXPECT_FALSE(q->full());
    EXPECT_EQ(q->size(), size_t{0});
}


TYPED_TEST(BoundedQueue, PopOnEmptyFailsAndLeavesTheArgumentUntouched) {
    auto q = TypeParam::template make<int, 16>();

    int val = 42;
    EXPECT_FALSE(q->pop(val));
    EXPECT_EQ(val, 42);
}

TYPED_TEST(BoundedQueue, TryPopOnEmptyReturnsNullopt) {
    auto q = TypeParam::template make<int, 16>();
    EXPECT_EQ(q->try_pop(), std::nullopt);
}

TYPED_TEST(BoundedQueue, QueueIsNotEmptyAfterPush) {
    auto q = TypeParam::template make<int, 16>();

    ASSERT_TRUE(q->push(1));
    EXPECT_FALSE(q->empty());
}

TYPED_TEST(BoundedQueue, QueueIsEmptyAgainOnceDrained) {
    auto q = TypeParam::template make<int, 16>();
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(q->push(i));

    int val = 0;
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(q->pop(val));

    EXPECT_TRUE(q->empty());
    EXPECT_FALSE(q->pop(val));
}

TYPED_TEST(BoundedQueue, PushFailsWhenFull) {
    auto q = TypeParam::template make<int, 16>();
    for (int i = 0; i < 16; ++i) ASSERT_TRUE(q->push(i));

    EXPECT_FALSE(q->push(999));
    EXPECT_FALSE(q->push(999)) << "a rejected push must not corrupt the queue";
}

TYPED_TEST(BoundedQueue, RejectedPushDoesNotOverwriteTheOldestElement) {
    auto q = TypeParam::template make<int, 4>();
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->push(i));
    ASSERT_FALSE(q->push(999));

    for (int i = 0; i < 4; ++i) {
        int val = -1;
        ASSERT_TRUE(q->pop(val));
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(q->empty());
}

TYPED_TEST(BoundedQueue, PopFreesExactlyOneSlot) {
    auto q = TypeParam::template make<int, 4>();
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->push(i));
    ASSERT_FALSE(q->push(100));

    int val = -1;
    ASSERT_TRUE(q->pop(val));
    EXPECT_TRUE(q->push(100));
    EXPECT_FALSE(q->push(101)) << "one pop must free one slot, not more";
}

TYPED_TEST(BoundedQueue, SizeTracksPushesAndPops) {
    auto q = TypeParam::template make<int, 8>();

    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(q->push(i));
        EXPECT_EQ(q->size(), static_cast<size_t>(i + 1));
    }
    EXPECT_TRUE(q->full());

    for (int i = 0; i < 8; ++i) {
        int val = -1;
        ASSERT_TRUE(q->pop(val));
        EXPECT_EQ(q->size(), static_cast<size_t>(7 - i));
        EXPECT_FALSE(q->full());
    }
    EXPECT_TRUE(q->empty());
}


TYPED_TEST(BoundedQueue, PopReturnsTheOldestElement) {
    auto q = TypeParam::template make<int, 16>();
    ASSERT_TRUE(q->push(1));
    ASSERT_TRUE(q->push(2));

    int val = 0;
    ASSERT_TRUE(q->pop(val));
    EXPECT_EQ(val, 1);
}

TYPED_TEST(BoundedQueue, TryPopReturnsTheOldestElement) {
    auto q = TypeParam::template make<int, 16>();
    ASSERT_TRUE(q->push(1));
    ASSERT_TRUE(q->push(2));

    EXPECT_EQ(q->try_pop(), 1);
    EXPECT_EQ(q->try_pop(), 2);
    EXPECT_EQ(q->try_pop(), std::nullopt);
}

TYPED_TEST(BoundedQueue, PushOfAnLvalueCopiesTheElement) {
    Counters c;
    {
        auto    q = TypeParam::template make<Tracked, 8>();
        Tracked source{&c, 5};

        ASSERT_TRUE(q->push(source));
        EXPECT_GT(c.copies.load(), 0) << "an lvalue push must copy, not steal the caller's element";
        EXPECT_EQ(source.value, 5);
    }
    EXPECT_EQ(c.live(), 0);
}

TYPED_TEST(BoundedQueue, PushOfAnRvalueMovesTheElement) {
    Counters c;
    {
        auto    q = TypeParam::template make<Tracked, 8>();
        Tracked source{&c, 5};

        const int copies_before = c.copies.load();
        ASSERT_TRUE(q->push(std::move(source)));
        EXPECT_EQ(c.copies.load(), copies_before) << "an rvalue push must not copy";
        EXPECT_GT(c.moves.load(), 0);
    }
    EXPECT_EQ(c.live(), 0);
}

TYPED_TEST(BoundedQueue, EmplaceConstructsTheElementFromItsArguments) {
    auto q = TypeParam::template make<std::string, 8>();

    ASSERT_TRUE(q->emplace(3, 'a'));
    ASSERT_TRUE(q->emplace("literal"));

    std::string out;
    ASSERT_TRUE(q->pop(out));
    EXPECT_EQ(out, "aaa");
    ASSERT_TRUE(q->pop(out));
    EXPECT_EQ(out, "literal");
}

TYPED_TEST(BoundedQueue, EmplaceFailsWhenFull) {
    auto q = TypeParam::template make<std::string, 4>();
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->emplace(2, 'x'));

    EXPECT_FALSE(q->emplace(2, 'x'));
    EXPECT_EQ(q->size(), size_t{4});
}

TYPED_TEST(BoundedQueue, PopOntoWritesThroughTheProvidedIterator) {
    auto q = TypeParam::template make<int, 8>();
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(q->push(i));

    std::vector<int> sink(3, -1);
    for (size_t i = 0; i < sink.size(); ++i) ASSERT_TRUE(q->pop_onto(sink.begin() + i));

    EXPECT_EQ(sink, (std::vector<int>{0, 1, 2}));
    EXPECT_TRUE(q->empty());
}

TYPED_TEST(BoundedQueue, PopOntoFailsWhenEmptyAndLeavesTheTargetUntouched) {
    auto q = TypeParam::template make<int, 8>();

    std::vector<int> sink{-1};
    EXPECT_FALSE(q->pop_onto(sink.begin()));
    EXPECT_EQ(sink.front(), -1);
}


TYPED_TEST(BoundedQueue, ConsumeOneAppliesTheFunctionToTheOldestElement) {
    auto q = TypeParam::template make<int, 8>();
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(q->push(i));

    int seen = -1;
    EXPECT_TRUE(q->consume_one([&](const int v) { seen = v; }));
    EXPECT_EQ(seen, 0);
    EXPECT_EQ(q->size(), size_t{2});
}

TYPED_TEST(BoundedQueue, ConsumeOneOnAnEmptyQueueFailsWithoutCalling) {
    auto q = TypeParam::template make<int, 8>();

    int calls = 0;
    EXPECT_FALSE(q->consume_one([&](int) { ++calls; }));
    EXPECT_EQ(calls, 0);
}

TYPED_TEST(BoundedQueue, ConsumeAllAvailableDrainsEveryElementInOrder) {
    auto q = TypeParam::template make<int, 8>();
    for (int i = 0; i < 8; ++i) ASSERT_TRUE(q->push(i));

    std::vector<int> seen;
    EXPECT_EQ(q->consume_all_available([&](const int v) { seen.push_back(v); }), size_t{8});

    EXPECT_EQ(seen, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    EXPECT_TRUE(q->empty());
}

TYPED_TEST(BoundedQueue, ConsumeAllAvailableOnAnEmptyQueueReportsZero) {
    auto q = TypeParam::template make<int, 8>();

    int calls = 0;
    EXPECT_EQ(q->consume_all_available([&](int) { ++calls; }), size_t{0});
    EXPECT_EQ(calls, 0);
}


TYPED_TEST(BoundedQueue, ResetEmptiesTheQueue) {
    auto q = TypeParam::template make<int, 8>();
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(q->push(i));

    q->reset();

    EXPECT_TRUE(q->empty());
    int val = -1;
    EXPECT_FALSE(q->pop(val));
}

TYPED_TEST(BoundedQueue, ResetDestroysTheElementsItDrops) {
    Counters c;
    {
        auto q = TypeParam::template make<Tracked, 8>();
        for (int i = 0; i < 5; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q->push(source));
        }
        ASSERT_EQ(c.live(), 5);

        q->reset();
        EXPECT_EQ(c.live(), 0) << "reset must destroy the elements it drops";
    }
    EXPECT_EQ(c.live(), 0);
}

TYPED_TEST(BoundedQueue, ResetOnAnEmptyQueueIsANoOp) {
    Counters c;
    {
        auto q = TypeParam::template make<Tracked, 8>();
        q->reset();

        EXPECT_TRUE(q->empty());
        EXPECT_EQ(c.constructed.load(), 0);
        EXPECT_EQ(c.destroyed.load(), 0);
    }
    EXPECT_EQ(c.live(), 0);
}

TYPED_TEST(BoundedQueue, QueueIsReusableAfterReset) {
    auto q = TypeParam::template make<int, 4>();
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->push(i));

    q->reset();

    for (int i = 100; i < 104; ++i) ASSERT_TRUE(q->push(i)) << "full capacity must be available again";
    EXPECT_FALSE(q->push(999));
    for (int i = 100; i < 104; ++i) {
        int val = -1;
        ASSERT_TRUE(q->pop(val));
        EXPECT_EQ(val, i);
    }
}

// -- Ordering and wrap-around ------------------------------------------------

TYPED_TEST(BoundedQueue, PreservesFifoOrder) {
    auto q = TypeParam::template make<int, 16>();
    for (int i = 0; i < 16; ++i) ASSERT_TRUE(q->push(i));

    for (int i = 0; i < 16; ++i) {
        int val = -1;
        ASSERT_TRUE(q->pop(val));
        EXPECT_EQ(val, i);
    }
}

TYPED_TEST(BoundedQueue, SurvivesManyLapsAroundTheBuffer) {
    auto q = TypeParam::template make<int, 4>();

    for (int lap = 0; lap < 100; ++lap) {
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->push(lap * 4 + i)) << "lap " << lap;
        for (int i = 0; i < 4; ++i) {
            int val = -1;
            ASSERT_TRUE(q->pop(val)) << "lap " << lap;
            EXPECT_EQ(val, lap * 4 + i);
        }
    }
}

TYPED_TEST(BoundedQueue, HoldsElementsSpanningTheWrapBoundary) {
    auto q = TypeParam::template make<int, 4>();

    // Advance head/tail so the live range straddles the end of the buffer.
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->push(i));
    int val = -1;
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(q->pop(val));
    for (int i = 4; i < 7; ++i) ASSERT_TRUE(q->push(i));

    for (int i = 3; i < 7; ++i) {
        ASSERT_TRUE(q->pop(val));
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(q->empty());
}

TYPED_TEST(BoundedQueue, MatchesAReferenceQueueUnderMixedOperations) {
    constexpr size_t cap = 8;

    auto            q = TypeParam::template make<int, cap>();
    std::deque<int> model;

    std::mt19937                       rng{12345};
    std::uniform_int_distribution<int> coin{0, 1};

    for (int i = 0; i < 2000; ++i) {
        if (coin(rng) == 0) {
            const bool pushed = q->push(i);
            EXPECT_EQ(pushed, model.size() < cap) << "at step " << i;
            if (pushed) model.push_back(i);
        } else {
            int        val    = -1;
            const bool popped = q->pop(val);
            EXPECT_EQ(popped, !model.empty()) << "at step " << i;
            if (popped) {
                EXPECT_EQ(val, model.front()) << "at step " << i;
                model.pop_front();
            }
        }
        ASSERT_EQ(q->empty(), model.empty()) << "at step " << i;
        ASSERT_EQ(q->size(), model.size()) << "at step " << i;
    }
}


TYPED_TEST(BoundedQueue, PushCopiesTheElementAndPopDestroysTheStoredCopy) {
    Counters c;
    {
        auto q = TypeParam::template make<Tracked, 8>();
        {
            Tracked source{&c, 7};
            ASSERT_TRUE(q->push(source));
            EXPECT_EQ(c.live(), 2) << "the source and the queue's copy";
        }
        EXPECT_EQ(c.live(), 1) << "the queue's copy outlives the source";

        Tracked sink{&c, 0};
        ASSERT_TRUE(q->pop(sink));
        EXPECT_EQ(sink.value, 7);
        EXPECT_EQ(c.live(), 1) << "pop must destroy the element it handed out";
    }
    EXPECT_EQ(c.live(), 0);
}

TYPED_TEST(BoundedQueue, EmplaceConstructsExactlyOneStoredElement) {
    Counters c;
    {
        auto q = TypeParam::template make<Tracked, 8>();
        ASSERT_TRUE(q->emplace(&c, 9));
        EXPECT_EQ(c.live(), 1);

        Tracked sink;
        ASSERT_TRUE(q->pop(sink));
        EXPECT_EQ(sink.value, 9);
        EXPECT_EQ(c.live(), 1) << "only the popped element is still alive";
    }
    EXPECT_EQ(c.live(), 0);
}

TYPED_TEST(BoundedQueue, TryPopHandsOutTheOnlyLiveCopy) {
    Counters c;
    {
        auto q = TypeParam::template make<Tracked, 8>();
        {
            Tracked source{&c, 3};
            ASSERT_TRUE(q->push(source));
        }
        ASSERT_EQ(c.live(), 1);

        auto popped = q->try_pop();
        ASSERT_TRUE(popped.has_value());
        EXPECT_EQ(popped->value, 3);
        EXPECT_EQ(c.live(), 1) << "only the returned element should remain alive";
    }
    EXPECT_EQ(c.live(), 0);
}

TYPED_TEST(BoundedQueue, DestructorDestroysElementsLeftInTheQueue) {
    Counters c;
    {
        auto q = TypeParam::template make<Tracked, 8>();
        for (int i = 0; i < 8; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q->push(source));
        }
        ASSERT_EQ(c.live(), 8);
    }
    EXPECT_EQ(c.live(), 0) << "elements still queued at destruction were leaked";
}

TYPED_TEST(BoundedQueue, DestructorDestroysElementsSpanningTheWrapBoundary) {
    Counters c;
    {
        auto    q = TypeParam::template make<Tracked, 4>();
        Tracked sink{&c, -1};

        for (int i = 0; i < 4; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q->push(source));
        }
        for (int i = 0; i < 3; ++i) ASSERT_TRUE(q->pop(sink));
        for (int i = 4; i < 7; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q->push(source));
        }
        ASSERT_EQ(c.live(), 5) << "four queued elements plus the sink";
    }
    EXPECT_EQ(c.live(), 0) << "elements wrapped past the buffer end were leaked";
}

TYPED_TEST(BoundedQueue, DestructorOfADrainedQueueDestroysNothing) {
    Counters c;
    {
        auto    q = TypeParam::template make<Tracked, 4>();
        Tracked sink{&c, -1};

        for (int i = 0; i < 4; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q->push(source));
        }
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->pop(sink));
        ASSERT_EQ(c.live(), 1) << "only the sink is still alive";
    }
    EXPECT_EQ(c.live(), 0);
    EXPECT_EQ(c.constructed.load(), c.destroyed.load()) << "a drained slot must not be destroyed twice";
}


TYPED_TEST(BoundedQueue, SlotsAreCorrectlyAlignedForOverAlignedElements) {
    auto q = TypeParam::template make<OverAligned, 4>();

    for (int lap = 0; lap < 3; ++lap) {
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->push(OverAligned{lap * 4 + i}));
        for (int i = 0; i < 4; ++i) {
            OverAligned out;
            ASSERT_TRUE(q->pop(out));
            EXPECT_EQ(out.value, lap * 4 + i);
            EXPECT_TRUE(out.aligned) << "element " << out.value << " was constructed on a misaligned slot";
        }
    }
}

TYPED_TEST(BoundedQueue, HandlesHeapAllocatingElements) {
    auto              q = TypeParam::template make<std::string, 4>();
    const std::string padding(256, 'x');

    for (int lap = 0; lap < 5; ++lap) {
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(q->push(padding + std::to_string(lap * 4 + i)));
        for (int i = 0; i < 4; ++i) {
            std::string out;
            ASSERT_TRUE(q->pop(out));
            EXPECT_EQ(out, padding + std::to_string(lap * 4 + i));
        }
    }
}

TYPED_TEST(BoundedQueue, PreservesElementsWiderThanAWord) {
    auto q = TypeParam::template make<Paired, 8>();

    for (std::uint64_t i = 0; i < 8; ++i) ASSERT_TRUE(q->push(Paired{i}));
    for (std::uint64_t i = 0; i < 8; ++i) {
        Paired out;
        ASSERT_TRUE(q->pop(out));
        EXPECT_EQ(out.a, i);
        EXPECT_TRUE(out.consistent());
    }
}

// -- Advertised traits -------------------------------------------------------

TYPED_TEST(BoundedQueue, AdvertisesItsElementTypeAndGuarantees) {
    using Q = typename TypeParam::template queue<int, 16>;

    static_assert(std::is_same_v<typename Q::value_t, int>);
    static_assert(bounded_queue_like<Q, int>);

    EXPECT_EQ(Q::model, TypeParam::model);
    EXPECT_TRUE(Q::lock_free);
    EXPECT_TRUE(Q::thread_safe);
}

// -- One producer, one consumer ----------------------------------------------

TYPED_TEST(BoundedQueue, HandsEveryElementFromOneProducerToOneConsumer) {
    auto                           q = TypeParam::template make<std::string, 16>();
    const std::vector<std::string> elements{"Hello", "World", "How", "Are", "You", "Today", "My", "Friend"};

    std::vector<std::string> popped;
    std::atomic_bool         complete{false};

    std::thread producer([&] {
        for (const auto &el: elements) {
            while (!q->push(el)) std::this_thread::yield();
        }
        complete.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::string out;
        while (!complete.load(std::memory_order_acquire)) {
            if (q->pop(out)) popped.push_back(out);
        }
        while (q->pop(out)) popped.push_back(out);
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(popped, elements);
    EXPECT_TRUE(q->empty());
}

TYPED_TEST(BoundedQueue, ProducerAndConsumerAgreeOnOrderUnderLoad) {
    // Capacity 16 against 200k elements keeps both the full and the empty path
    // hot for the whole run.
    constexpr int TOTAL = 200000;

    auto             q = TypeParam::template make<int, 16>();
    std::atomic<int> received{0};
    std::atomic<int> out_of_order{0};

    std::thread producer([&] {
        for (int i = 0; i < TOTAL; ++i)
            while (!q->push(i)) std::this_thread::yield();
    });

    std::thread consumer([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        int        expected = 0;
        int        val      = 0;
        while (expected < TOTAL) {
            if (q->pop(val)) {
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
    EXPECT_TRUE(q->empty());
}

TYPED_TEST(BoundedQueue, NonTrivialElementsSurviveTheHandover) {
    constexpr int TOTAL = 20000;

    auto             q = TypeParam::template make<std::string, 16>();
    std::atomic<int> mismatches{0};
    std::atomic<int> received{0};

    const auto payload_for = [](const int i) { return std::string(64, 'x') + std::to_string(i); };

    std::thread producer([&] {
        for (int i = 0; i < TOTAL; ++i) {
            const std::string payload = payload_for(i);
            while (!q->push(payload)) std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        const auto  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        std::string out;
        int         expected = 0;
        while (expected < TOTAL) {
            if (q->pop(out)) {
                if (out != payload_for(expected)) mismatches.fetch_add(1, std::memory_order_relaxed);
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

TYPED_TEST(BoundedQueue, ElementsHandedOverBetweenThreadsAreNotLeaked) {
    constexpr int TOTAL = 5000;

    Counters c;
    {
        auto             q = TypeParam::template make<Tracked, 16>();
        std::atomic<int> received{0};

        std::thread producer([&] {
            for (int i = 0; i < TOTAL; ++i)
                while (!q->emplace(&c, i)) std::this_thread::yield();
        });

        std::thread consumer([&] {
            Tracked    sink;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (received.load(std::memory_order_relaxed) < TOTAL) {
                if (q->pop(sink)) received.fetch_add(1, std::memory_order_relaxed);
                else if (std::chrono::steady_clock::now() > deadline) break;
            }
        });

        producer.join();
        consumer.join();

        ASSERT_EQ(received.load(), TOTAL);
        EXPECT_TRUE(q->empty());
    }
    EXPECT_EQ(c.live(), 0) << "elements handed between threads were leaked";
}
} // namespace dcl::test
