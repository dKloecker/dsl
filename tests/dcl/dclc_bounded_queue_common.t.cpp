//
// Created by Dominic Kloecker on 16/09/2026.
//
// Contract every bounded queue shares: push/pop/empty, the capacity rounding,
// FIFO order, behaviour at the full and empty boundaries, wrap-around, and
// element lifetimes. Run once per queue flavour via TYPED_TEST.
//
#include <deque>
#include <random>
#include <string>
#include <gtest/gtest.h>

#include "dclc_bounded_queue_test_types.h"

namespace dcl::test {
template<typename Tag>
class BoundedQueue : public ::testing::Test {};

TYPED_TEST_SUITE(BoundedQueue, QueueTags, QueueTagNames);

// -- Capacity ---------------------------------------------------------------

TYPED_TEST(BoundedQueue, CapacityRoundsRequestUpToPowerOfTwo) {
    EXPECT_EQ((TypeParam::template queue<int, 5>::capacity), size_t{8});
    EXPECT_EQ((TypeParam::template queue<int, 9>::capacity), size_t{16});
}

TYPED_TEST(BoundedQueue, ExactPowerOfTwoCapacityIsLeftAlone) {
    EXPECT_EQ((TypeParam::template queue<int, 16>::capacity), size_t{16});
}

TYPED_TEST(BoundedQueue, RoundedUpCapacityIsFullyUsable) {
    typename TypeParam::template queue<int, 5> q;  // rounds up to 8

    for (int i = 0; i < 8; ++i) ASSERT_TRUE(q.push(i)) << "push " << i << " of 8 rejected";
    EXPECT_FALSE(q.push(999));

    for (int i = 0; i < 8; ++i) {
        int val = -1;
        ASSERT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
}

// -- Empty / full boundaries -------------------------------------------------

TYPED_TEST(BoundedQueue, NewQueueIsEmpty) {
    typename TypeParam::template queue<int, 16> q;
    EXPECT_TRUE(q.empty());
}

TYPED_TEST(BoundedQueue, PopOnEmptyFailsAndLeavesTheArgumentUntouched) {
    typename TypeParam::template queue<int, 16> q;

    int val = 42;
    EXPECT_FALSE(q.pop(val));
    EXPECT_EQ(val, 42);
}

TYPED_TEST(BoundedQueue, QueueIsNotEmptyAfterPush) {
    typename TypeParam::template queue<int, 16> q;
    ASSERT_TRUE(q.push(1));
    EXPECT_FALSE(q.empty());
}

TYPED_TEST(BoundedQueue, QueueIsEmptyAgainOnceDrained) {
    typename TypeParam::template queue<int, 16> q;
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(q.push(i));

    int val = 0;
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(q.pop(val));

    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.pop(val));
}

TYPED_TEST(BoundedQueue, PushFailsWhenFull) {
    typename TypeParam::template queue<int, 16> q;
    for (int i = 0; i < 16; ++i) ASSERT_TRUE(q.push(i));

    EXPECT_FALSE(q.push(999));
    EXPECT_FALSE(q.push(999)) << "a rejected push must not corrupt the queue";
}

TYPED_TEST(BoundedQueue, RejectedPushDoesNotOverwriteTheOldestElement) {
    typename TypeParam::template queue<int, 4> q;
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(i));
    ASSERT_FALSE(q.push(999));

    for (int i = 0; i < 4; ++i) {
        int val = -1;
        ASSERT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(q.empty());
}

TYPED_TEST(BoundedQueue, PopFreesExactlyOneSlot) {
    typename TypeParam::template queue<int, 4> q;
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(i));
    ASSERT_FALSE(q.push(100));

    int val = -1;
    ASSERT_TRUE(q.pop(val));
    EXPECT_TRUE(q.push(100));
    EXPECT_FALSE(q.push(101)) << "one pop must free one slot, not more";
}

// -- Ordering and wrap-around ------------------------------------------------

TYPED_TEST(BoundedQueue, PreservesFifoOrder) {
    typename TypeParam::template queue<int, 16> q;
    for (int i = 0; i < 16; ++i) ASSERT_TRUE(q.push(i));

    for (int i = 0; i < 16; ++i) {
        int val = -1;
        ASSERT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
}

TYPED_TEST(BoundedQueue, SurvivesManyLapsAroundTheBuffer) {
    typename TypeParam::template queue<int, 4> q;

    for (int lap = 0; lap < 100; ++lap) {
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(lap * 4 + i)) << "lap " << lap;
        for (int i = 0; i < 4; ++i) {
            int val = -1;
            ASSERT_TRUE(q.pop(val)) << "lap " << lap;
            EXPECT_EQ(val, lap * 4 + i);
        }
    }
}

TYPED_TEST(BoundedQueue, HoldsElementsSpanningTheWrapBoundary) {
    typename TypeParam::template queue<int, 4> q;

    // Advance head/tail so the live range straddles the end of the buffer.
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(i));
    int val = -1;
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(q.pop(val));
    for (int i = 4; i < 7; ++i) ASSERT_TRUE(q.push(i));

    for (int i = 3; i < 7; ++i) {
        ASSERT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(q.empty());
}

TYPED_TEST(BoundedQueue, MatchesAReferenceQueueUnderMixedOperations) {
    constexpr size_t                             cap = 8;
    typename TypeParam::template queue<int, cap> q;
    std::deque<int>                              model;

    std::mt19937                       rng{12345};
    std::uniform_int_distribution<int> coin{0, 1};

    for (int i = 0; i < 2000; ++i) {
        if (coin(rng) == 0) {
            const bool pushed = q.push(i);
            EXPECT_EQ(pushed, model.size() < cap) << "at step " << i;
            if (pushed) model.push_back(i);
        } else {
            int        val    = -1;
            const bool popped = q.pop(val);
            EXPECT_EQ(popped, !model.empty()) << "at step " << i;
            if (popped) {
                EXPECT_EQ(val, model.front()) << "at step " << i;
                model.pop_front();
            }
        }
        ASSERT_EQ(q.empty(), model.empty()) << "at step " << i;
    }
}

// -- Element lifetimes -------------------------------------------------------

TYPED_TEST(BoundedQueue, PushCopiesTheElementAndPopDestroysTheStoredCopy) {
    Counters c;
    {
        typename TypeParam::template queue<Tracked, 8> q;
        {
            Tracked source{&c, 7};
            ASSERT_TRUE(q.push(source));
            EXPECT_EQ(c.live(), 2) << "the source and the queue's copy";
        }
        EXPECT_EQ(c.live(), 1) << "the queue's copy outlives the source";

        Tracked sink{&c, 0};
        ASSERT_TRUE(q.pop(sink));
        EXPECT_EQ(sink.value, 7);
        EXPECT_EQ(c.live(), 1) << "pop must destroy the element it handed out";
    }
    EXPECT_EQ(c.live(), 0);
}

TYPED_TEST(BoundedQueue, DestructorDestroysElementsLeftInTheQueue) {
    Counters c;
    {
        typename TypeParam::template queue<Tracked, 8> q;
        for (int i = 0; i < 8; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q.push(source));
        }
        ASSERT_EQ(c.live(), 8);
    }
    EXPECT_EQ(c.live(), 0) << "elements still queued at destruction were leaked";
}

TYPED_TEST(BoundedQueue, DestructorDestroysElementsSpanningTheWrapBoundary) {
    Counters c;
    {
        typename TypeParam::template queue<Tracked, 4> q;
        Tracked                                        sink{&c, -1};

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
    }
    EXPECT_EQ(c.live(), 0) << "elements wrapped past the buffer end were leaked";
}

TYPED_TEST(BoundedQueue, DestructorOfAnEmptyQueueDestroysNothing) {
    Counters c;
    {
        typename TypeParam::template queue<Tracked, 4> q;
        Tracked                                        sink{&c, -1};
        for (int i = 0; i < 4; ++i) {
            Tracked source{&c, i};
            ASSERT_TRUE(q.push(source));
        }
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.pop(sink));
        ASSERT_EQ(c.live(), 1) << "only the sink is still alive";
    }
    EXPECT_EQ(c.live(), 0);
    EXPECT_EQ(c.constructed, c.destroyed) << "a drained slot must not be destroyed twice";
}

// -- Storage properties ------------------------------------------------------

TYPED_TEST(BoundedQueue, SlotsAreCorrectlyAlignedForOverAlignedElements) {
    using Q = typename TypeParam::template queue<OverAligned, 4>;
    EXPECT_GE(alignof(Q), alignof(OverAligned))
        << "the queue is less aligned than its element type, so its raw storage "
           "cannot be guaranteed suitably aligned for placement new";

    Q q;
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(OverAligned{i}));
    for (int i = 0; i < 4; ++i) {
        OverAligned out;
        ASSERT_TRUE(q.pop(out));
        EXPECT_EQ(out.value, i);
        EXPECT_TRUE(out.aligned) << "element " << i << " was constructed on a misaligned slot";
    }
}

TYPED_TEST(BoundedQueue, HandlesHeapAllocatingElements) {
    typename TypeParam::template queue<std::string, 4> q;
    const std::string                                  padding(256, 'x');

    for (int lap = 0; lap < 5; ++lap) {
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(padding + std::to_string(lap * 4 + i)));
        for (int i = 0; i < 4; ++i) {
            std::string out;
            ASSERT_TRUE(q.pop(out));
            EXPECT_EQ(out, padding + std::to_string(lap * 4 + i));
        }
    }
}

TYPED_TEST(BoundedQueue, PreservesElementsWiderThanAWord) {
    typename TypeParam::template queue<Paired, 8> q;

    for (std::uint64_t i = 0; i < 8; ++i) ASSERT_TRUE(q.push(Paired{i}));
    for (std::uint64_t i = 0; i < 8; ++i) {
        Paired out;
        ASSERT_TRUE(q.pop(out));
        EXPECT_EQ(out.a, i);
        EXPECT_TRUE(out.consistent());
    }
}
} // namespace dcl::test
