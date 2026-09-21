//
// Contended comparison: the same queues under several producers and consumers at
// once. Only the queues whose concurrency model actually allows it are
// registered -- see the DSL_BENCH_ALL_MPSC / DSL_BENCH_ALL_SPMC /
// DSL_BENCH_ALL_MPMC rosters in dclc_bounded_queue_bench_types.h.
//
// Thread counts come in as arguments so one workload covers every shape:
// Args({producers, consumers}), the main (timed) thread counting as a producer.
// Reported throughput is the main thread's share of the pushes; the background
// threads exist to keep the queue contended, so compare shapes within a queue
// and queues within a shape, not throughput across different shapes.
//
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

#include "dclc_bounded_queue_bench_types.h"

namespace dcl::bench {
namespace {

/**
 * `Args({producers, consumers})` threads hammering one queue. The timed loop is
 * the main thread's share of the production; every other producer and all
 * consumers spin in the background for as long as the benchmark runs, so the
 * measured pushes see a realistically busy queue (CAS retries on the tail,
 * slots being released underneath, the head line bouncing between cores).
 */
template<bench_queue Adapter>
void BM_Vs_Contended(benchmark::State &state) {
    using value_type = typename Adapter::value_type;

    const auto producers = static_cast<std::size_t>(state.range(0));
    const auto consumers = static_cast<std::size_t>(state.range(1));

    auto             q = std::make_unique<Adapter>();
    std::atomic_bool done{false};

    std::vector<std::thread> workers;
    workers.reserve(producers - 1 + consumers);

    for (std::size_t i = 0; i < consumers; i++) {
        workers.emplace_back([&] {
            value_type out;
            while (!done.load(std::memory_order_acquire)) {
                q->pop(out);
            }
            // Drain so a producer blocked on a full queue can always finish.
            while (q->pop(out)) {}
            benchmark::DoNotOptimize(out);
        });
    }
    for (std::size_t i = 1; i < producers; i++) {
        workers.emplace_back([&] {
            while (!done.load(std::memory_order_acquire)) {
                q->push({});
            }
        });
    }

    std::uint64_t pushed = 0;
    for (auto _: state) {
        while (!q->push({})) {}
        pushed++;
    }

    done.store(true, std::memory_order_release);
    for (auto &w: workers) w.join();
    state.SetItemsProcessed(static_cast<std::int64_t>(pushed));
}


#define DSL_CONTENDED_SHAPE(Roster, Producers, Consumers)                  \
    Roster(BM_Vs_Contended, 1024,                                          \
           ->ArgNames({"producers", "consumers"})                          \
               ->Args({Producers, Consumers})                              \
                   ->UseRealTime()                                         \
                       ->MinWarmUpTime(0.2))

// Many producers, one consumer
DSL_CONTENDED_SHAPE(DSL_BENCH_ALL_MPSC, 2, 1)
DSL_CONTENDED_SHAPE(DSL_BENCH_ALL_MPSC, 4, 1)

// One producer, many consumers
DSL_CONTENDED_SHAPE(DSL_BENCH_ALL_SPMC, 1, 2)
DSL_CONTENDED_SHAPE(DSL_BENCH_ALL_SPMC, 1, 4)

// Symmetric contention, including {1, 1} so the price of the multi-producer
// algorithm at zero contention is visible next to the SPSC numbers.
DSL_CONTENDED_SHAPE(DSL_BENCH_ALL_MPMC, 1, 1)
DSL_CONTENDED_SHAPE(DSL_BENCH_ALL_MPMC, 2, 2)
DSL_CONTENDED_SHAPE(DSL_BENCH_ALL_MPMC, 4, 4)

} // namespace
} // namespace dcl::bench
