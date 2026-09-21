// Uncontended comparison: every bounded queue in the library, plus the boost::lockfree references
// driven by a single producer and a single consumer to provide a generalised measurement

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

#include <benchmark/benchmark.h>

#include "dclc_bounded_queue_bench_types.h"

namespace dcl::bench {
namespace {

template<bench_queue Adapter>
auto make_queue() { return std::make_unique<Adapter>(); }

// -- Uncontended push/pop ----------------------------------------------------

/// Alternating push and pop on a near-empty queue: producer and consumer
/// indices stay hot in the same core's cache, so this isolates the per-operation
/// bookkeeping (index arithmetic, sequence counters, element copy).
template<bench_queue Adapter>
void BM_Vs_PushPop(benchmark::State &state) {
    constexpr std::size_t batch = 10'000;

    auto                         q = make_queue<Adapter>();
    typename Adapter::value_type out;

    for (auto _: state) {
        for (std::size_t i = 0; i < batch; i++) {
            q->push({});
            q->pop(out);
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * batch));
}

DSL_BENCH_ALL_SPSC(BM_Vs_PushPop, 1024, ->MinWarmUpTime(1.0))

/// The same alternation, but with the queue held one element short of full, so
/// every push lands on the slot a pop has just released.
template<bench_queue Adapter>
void BM_Vs_PushPopNearFull(benchmark::State &state) {
    constexpr std::size_t batch = 10'000;

    auto q = make_queue<Adapter>();
    while (q->push({})) {}

    typename Adapter::value_type out;
    q->pop(out);

    for (auto _: state) {
        for (std::size_t i = 0; i < batch; i++) {
            q->push({});
            q->pop(out);
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * batch));
}

DSL_BENCH_ALL_SPSC(BM_Vs_PushPopNearFull, 1024, ->MinWarmUpTime(1.0))

/// Fill the queue to rejection, then drain it to empty: a full sweep of the ring
/// per iteration, so this is the streaming/bulk-transfer cost rather than the
/// single-slot cost. Items are counted rather than derived from the requested
/// capacity, because not every queue makes all of its slots usable.
template<bench_queue Adapter>
void BM_Vs_FillAndDrain(benchmark::State &state) {
    auto          q     = make_queue<Adapter>();
    std::uint64_t items = 0;

    for (auto _: state) {
        while (q->push({})) items++;

        typename Adapter::value_type out;
        while (q->pop(out)) items++;
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(items));
}

DSL_BENCH_ALL_SPSC(BM_Vs_FillAndDrain, 8192, ->MinWarmUpTime(1.0))

// -- One producer, one consumer, two threads ---------------------------------

/// A real producer/consumer pair on separate threads: the timed loop is the
/// producer, a background thread consumes. This is the first workload where the
/// head and tail counters live in different caches, so it exposes the false
/// sharing and memory-ordering costs the single-threaded workloads hide.
template<bench_queue Adapter>
void BM_Vs_ProducerConsumer(benchmark::State &state) {
    auto             q = make_queue<Adapter>();
    std::atomic_bool done{false};

    std::thread consumer([&] {
        typename Adapter::value_type out;
        while (!done.load(std::memory_order_acquire)) {
            q->pop(out);
        }
        while (q->pop(out)) {}
        benchmark::DoNotOptimize(out);
    });

    std::uint64_t pushed = 0;
    for (auto _: state) {
        while (!q->push({})) {}
        pushed++;
    }

    done.store(true, std::memory_order_release);
    consumer.join();
    state.SetItemsProcessed(static_cast<std::int64_t>(pushed));
}

DSL_BENCH_ALL_SPSC(BM_Vs_ProducerConsumer, 1024, ->MinWarmUpTime(0.5)->UseRealTime())

} // namespace
} // namespace dcl::bench
