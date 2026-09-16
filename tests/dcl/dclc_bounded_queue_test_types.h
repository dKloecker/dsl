//
// Created by Dominic Kloecker on 16/09/2026.
//
// Shared fixtures for the bounded queue suites: instrumentation types that make
// element lifetimes and slot alignment observable, plus the tag list that drives
// the typed tests over every queue flavour.
//
#ifndef DSL_TESTS_DCLC_BOUNDED_QUEUE_TEST_TYPES_H_
#define DSL_TESTS_DCLC_BOUNDED_QUEUE_TEST_TYPES_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <gtest/gtest.h>

#include "dclc_mpmc_bounded_queue.h"
#include "dclc_mpsc_bounded_queue.h"
#include "dclc_spsc_bounded_queue.h"

namespace dcl::test {

/**
 * Lifetime ledger for Tracked. `live()` is the number of Tracked objects that
 * have been constructed but not yet destroyed -- it must return to zero once
 * every owner (including the queue itself) has gone away.
 */
struct Counters {
    int constructed = 0;
    int destroyed   = 0;
    int copies      = 0;
    int moves       = 0;

    int live() const { return constructed - destroyed; }
};

/**
 * Element type that reports every construction and destruction. Assignment does
 * not touch the ledger: it transfers a value between two already-live objects.
 */
struct Tracked {
    Counters *counters;
    int       value;

    Tracked(Counters *c, const int v)
        : counters(c), value(v) { ++counters->constructed; }

    Tracked(const Tracked &other)
        : counters(other.counters), value(other.value) {
        ++counters->constructed;
        ++counters->copies;
    }

    Tracked(Tracked &&other) noexcept
        : counters(other.counters), value(other.value) {
        ++counters->constructed;
        ++counters->moves;
    }

    Tracked &operator=(const Tracked &other) {
        counters = other.counters;
        value    = other.value;
        return *this;
    }

    Tracked &operator=(Tracked &&other) noexcept {
        counters = other.counters;
        value    = other.value;
        return *this;
    }

    ~Tracked() { ++counters->destroyed; }

    friend bool operator==(const Tracked &l, const Tracked &r) { return l.value == r.value; }
};

/**
 * Over-aligned element. `aligned` records whether the object was placed on a
 * correctly aligned address, recomputed on construction (which is what the queue
 * does when it placement-news into its raw buffer) and carried through moves
 * (which is how pop() hands the answer back out).
 */
struct alignas(128) OverAligned {
    int  value;
    bool aligned;

    static bool is_aligned(const void *p) {
        return reinterpret_cast<std::uintptr_t>(p) % 128 == 0;
    }

    explicit OverAligned(const int v = 0)
        : value(v), aligned(is_aligned(this)) {}

    OverAligned(const OverAligned &other)
        : value(other.value), aligned(is_aligned(this)) {}

    OverAligned(OverAligned &&other) noexcept
        : value(other.value), aligned(other.aligned) {}

    OverAligned &operator=(const OverAligned &other) = default;
    OverAligned &operator=(OverAligned &&other)      = default;
    ~OverAligned()                                   = default;
};

/**
 * Element whose two halves must always agree. A torn or half-published write
 * between concurrent producers shows up as `b != ~a`.
 */
struct Paired {
    std::uint64_t a = 0;
    std::uint64_t b = 0;

    Paired() = default;

    explicit Paired(const std::uint64_t v)
        : a(v), b(~v) {}

    bool consistent() const { return b == ~a; }
};

/** Spins on @p pred, giving up after @p timeout so a deadlock fails instead of hanging. */
template<typename Pred>
bool spin_until(Pred pred, const std::chrono::milliseconds timeout = std::chrono::milliseconds{10000}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

// -- Tags binding the shared typed tests to each queue flavour ----------------

struct spsc_tag {
    static constexpr const char *name = "spsc";

    template<typename T, size_t RequestedCapacity>
    using queue = dcl::spsc_bounded_queue<T, RequestedCapacity>;
};

struct mpsc_tag {
    static constexpr const char *name = "mpsc";

    template<typename T, size_t RequestedCapacity>
    using queue = dcl::mpsc_bounded_queue<T, RequestedCapacity>;
};

struct mpmc_tag {
    static constexpr const char *name = "mpmc";

    template<typename T, size_t RequestedCapacity>
    using queue = dcl::mpmc_bounded_queue<T, RequestedCapacity>;
};

using QueueTags = ::testing::Types<spsc_tag, mpsc_tag, mpmc_tag>;

class QueueTagNames {
public:
    template<typename Tag>
    static std::string GetName(int) { return Tag::name; }
};
} // namespace dcl::test

#endif  // DSL_TESTS_DCLC_BOUNDED_QUEUE_TEST_TYPES_H_
