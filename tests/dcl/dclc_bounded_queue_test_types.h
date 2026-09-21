//
// Created by Dominic Kloecker on 16/09/2026.
//
// Shared fixtures for the bounded queue suite: instrumentation types that make
// element lifetimes and slot alignment observable, plus the tag list that drives
// the typed tests over every queue flavour.
//

#ifndef DSL_TESTS_DCLC_BOUNDED_QUEUE_TEST_TYPES_H_
#define DSL_TESTS_DCLC_BOUNDED_QUEUE_TEST_TYPES_H_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <gtest/gtest.h>

#include "dclc_bounded_queue.h"

namespace dcl::test {

/**
 * Lifetime ledger for Tracked. `live()` is the number of Tracked objects that
 * have been constructed but not yet destroyed -- it must return to zero once
 * every owner (including the queue itself) has gone away.
 */
struct Counters {
    std::atomic<int> constructed{0};
    std::atomic<int> destroyed{0};
    std::atomic<int> copies{0};
    std::atomic<int> moves{0};

    int live() const {
        return constructed.load(std::memory_order_relaxed) - destroyed.load(std::memory_order_relaxed);
    }
};

/**
 * Element type that reports every construction and destruction.
 *
 */
struct Tracked {
    Counters *counters = nullptr;
    int       value    = 0;

    Tracked() = default;

    Tracked(Counters *c, const int v)
        : counters(c), value(v) { if (counters) ++counters->constructed; }

    Tracked(const Tracked &other)
        : counters(other.counters), value(other.value) {
        if (counters) {
            ++counters->constructed;
            ++counters->copies;
        }
    }

    Tracked(Tracked &&other) noexcept
        : counters(other.counters), value(other.value) {
        if (counters) {
            ++counters->constructed;
            ++counters->moves;
        }
    }

    Tracked &operator=(const Tracked &other) {
        rebind(other.counters);
        value = other.value;
        return *this;
    }

    Tracked &operator=(Tracked &&other) noexcept {
        rebind(other.counters);
        value = other.value;
        return *this;
    }

    ~Tracked() { if (counters) ++counters->destroyed; }

    friend bool operator==(const Tracked &l, const Tracked &r) { return l.value == r.value; }

private:
    void rebind(Counters *c) {
        if (counters == c) return;
        if (counters) ++counters->destroyed;
        counters = c;
        if (counters) ++counters->constructed;
    }
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
 * shows up as `b != ~a`.
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

/// Queue Types (fixed and dynamic size)

/// Capacity baked into the type.
template<concurrency C>
struct fixed_capacity {
    static constexpr concurrency model         = C;
    static constexpr bool        runtime_sized = false;

    template<typename T, std::size_t Capacity>
    using queue = dcl::bounded_queue<T, C, Capacity>;

    template<typename T, std::size_t Capacity>
    static auto make() { return std::make_unique<queue<T, Capacity>>(); }
};

/// Capacity supplied to the constructor.
template<concurrency C>
struct dynamic_capacity {
    static constexpr concurrency model         = C;
    static constexpr bool        runtime_sized = true;

    template<typename T, std::size_t Capacity>
    using queue = dcl::bounded_queue<T, C>;

    template<typename T, std::size_t Capacity>
    static auto make() { return std::make_unique<queue<T, Capacity>>(Capacity); }
};

struct spsc_fixed_tag : fixed_capacity<concurrency::swsr> {
    static constexpr const char *name = "spsc_fixed";
};

struct spsc_dyn_tag : dynamic_capacity<concurrency::swsr> {
    static constexpr const char *name = "spsc_dyn";
};

struct spmc_fixed_tag : fixed_capacity<concurrency::swmr> {
    static constexpr const char *name = "spmc_fixed";
};

struct spmc_dyn_tag : dynamic_capacity<concurrency::swmr> {
    static constexpr const char *name = "spmc_dyn";
};

struct mpsc_fixed_tag : fixed_capacity<concurrency::mwsr> {
    static constexpr const char *name = "mpsc_fixed";
};

struct mpsc_dyn_tag : dynamic_capacity<concurrency::mwsr> {
    static constexpr const char *name = "mpsc_dyn";
};

struct mpmc_fixed_tag : fixed_capacity<concurrency::mwmr> {
    static constexpr const char *name = "mpmc_fixed";
};

struct mpmc_dyn_tag : dynamic_capacity<concurrency::mwmr> {
    static constexpr const char *name = "mpmc_dyn";
};

using QueueTags = ::testing::Types<
    spsc_fixed_tag,
    spsc_dyn_tag,
    spmc_fixed_tag,
    spmc_dyn_tag,
    mpsc_fixed_tag,
    mpsc_dyn_tag,
    mpmc_fixed_tag,
    mpmc_dyn_tag
>;

class QueueTagNames {
public:
    template<typename Tag>
    static std::string GetName(int) { return Tag::name; }
};
} // namespace dcl::test

#endif  // DSL_TESTS_DCLC_BOUNDED_QUEUE_TEST_TYPES_H_
