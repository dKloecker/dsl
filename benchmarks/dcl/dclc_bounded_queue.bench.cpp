//
// Behaviour of dcl::bounded_queue itself: how the queue behaves while it fills,
// while it drains, in steady state, and under a configurable producer/consumer
// mix. Every point is registered for all four concurrency models in both their
// compile-time sized and runtime sized form, over three payloads.
//
// Benchmarks are named "<workload>/<model>.<sizing>/<payload>/<capacity>", so a
// slice can be picked out with e.g.
//   ./dcl_benchmarks --benchmark_filter='Throughput/mpmc\.'
//   ./dcl_benchmarks --benchmark_filter='/string/'
//
// The parameterised arguments are:
//   fill_pct   how full the queue is when the measurement starts (0 = from new,
//              100 = already full), as a percentage of capacity
//   producers  threads pushing
//   consumers  threads popping
//   elements   how many elements must be produced and drained per iteration
//
#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

#include "dclc_bounded_queue.h"

namespace dcl::bench {

// -- Payloads ----------------------------------------------------------------

template<typename P>
concept bench_payload = std::default_initializable<typename P::type> && requires {
    { P::name } -> std::convertible_to<const char *>;
    { P::sample() } -> std::convertible_to<const typename P::type &>;
};

/// Word-sized and trivially copied: the queue's own bookkeeping dominates.
struct int_payload {
    using type = int;

    static constexpr const char *name = "int";

    static const type &sample() {
        static constexpr type value = 42;
        return value;
    }
};

/// Heap-owning: every transfer is a copy into the slot and a move back out.
struct string_payload {
    using type = std::string;

    inline static const type value{"bounded-queue-benchmark-payload"};

    static constexpr const char *name = "string";

    static const type &sample() { return value; }
};

/// Several members of mixed kinds, one of them owning: a realistic message.
struct ComplexObject {
    std::uint64_t             id     = 0;
    double                    weight = 0.0;
    std::array<std::byte, 32> digest{};
    std::string               label;
    std::vector<int>          tags;
};

struct complex_payload {
    using type = ComplexObject;

    inline static const type value{
        .id     = 7,
        .weight = 3.5,
        .digest = {},
        .label  = "bounded-queue-benchmark-payload",
        .tags   = {1, 2, 3, 4, 5, 6, 7, 8}
    };

    static constexpr const char *name = "complex";

    static const type &sample() { return value; }
};

// -- Queue flavours ----------------------------------------------------------

enum class sizing : std::uint8_t { fixed, dynamic };

constexpr const char *sizing_name(const sizing s) { return s == sizing::fixed ? "fixed" : "dynamic"; }

constexpr const char *model_name(const concurrency c) {
    switch (c) {
        case concurrency::SPSC: return "spsc";
        case concurrency::SPMC: return "spmc";
        case concurrency::MPSC: return "mpsc";
        case concurrency::MPMC: return "mpmc";
        default: return "unknown";
    }
}

/**
 * One queue plus the uniform surface the workloads are written against. The two
 * sizings differ only in where the capacity comes from -- the type for the fixed
 * one, the constructor for the dynamic one -- so everything else lives here.
 */
template<bench_payload P, concurrency C, sizing S, std::size_t Capacity, typename Queue>
struct queue_holder {
    using payload    = P;
    using value_type = typename P::type;

    static constexpr std::size_t capacity = Capacity;
    static constexpr concurrency model    = C;
    static constexpr sizing      sized    = S;

    Queue q;

    queue_holder() requires std::default_initializable<Queue> = default;

    explicit queue_holder(const std::size_t runtime_capacity)
        : q(runtime_capacity) {}

    static const value_type &sample() { return P::sample(); }

    bool        push(const value_type &in) { return q.push(in); }
    bool        pop(value_type &out) { return q.pop(out); }
    std::size_t size() const { return q.size(); }
};

template<bench_payload P, concurrency C, sizing S, std::size_t Capacity>
struct queue_under_test;

template<bench_payload P, concurrency C, std::size_t Capacity>
struct queue_under_test<P, C, sizing::fixed, Capacity>
    : queue_holder<P, C, sizing::fixed, Capacity, dcl::bounded_queue<typename P::type, C, Capacity>> {};

template<bench_payload P, concurrency C, std::size_t Capacity>
struct queue_under_test<P, C, sizing::dynamic, Capacity>
    : queue_holder<P, C, sizing::dynamic, Capacity, dcl::bounded_queue<typename P::type, C>> {
    using base = queue_holder<P, C, sizing::dynamic, Capacity, dcl::bounded_queue<typename P::type, C>>;

    queue_under_test()
        : base(Capacity) {}
};

/// What a workload requires of anything it is registered against.
template<typename Q>
concept bench_queue = bench_payload<typename Q::payload> &&
                      requires(Q q, const typename Q::value_type &in, typename Q::value_type &out) {
                          { q.push(in) } -> std::same_as<bool>;
                          { q.pop(out) } -> std::same_as<bool>;
                          { q.size() } -> std::convertible_to<std::size_t>;
                          { Q::sample() } -> std::convertible_to<const typename Q::value_type &>;
                          { Q::capacity } -> std::convertible_to<std::size_t>;
                      };

// -- Shared helpers ----------------------------------------------------------

/// The queues carry their buffer inline, so at large capacities they are far too
/// big for a thread stack -- and heap-allocating both sizings keeps the fixed and
/// dynamic numbers comparable.
template<bench_queue Q>
auto make_queue() { return std::make_unique<Q>(); }

constexpr std::size_t occupancy(const std::size_t capacity, const std::int64_t fill_pct) {
    return static_cast<std::size_t>(capacity * static_cast<std::size_t>(fill_pct) / 100);
}

/// Brings the queue to exactly @p target elements, pushing or popping as needed.
template<bench_queue Q>
void set_occupancy(Q &q, const std::size_t target) {
    typename Q::value_type out;
    while (q.size() > target) q.pop(out);
    while (q.size() < target) q.push(Q::sample());
}

/// "<workload>/<model>.<sizing>/<payload>/<capacity>" -- see the filter note above.
template<bench_queue Q>
std::string bench_name(const std::string_view workload) {
    return std::string{workload}
           .append("/")
           .append(model_name(Q::model))
           .append(".")
           .append(sizing_name(Q::sized))
           .append("/")
           .append(Q::payload::name)
           .append("/")
           .append(std::to_string(Q::capacity));
}

namespace {

// -- Filling, draining, and steady state -------------------------------------

/// From `fill_pct` full to rejection: the cost of closing the gap to full, which
/// is where a producer spends its time when it is outrunning the consumer.
template<bench_queue Q>
void BM_Fill(benchmark::State &state) {
    const std::size_t start = occupancy(Q::capacity, state.range(0));

    auto          q      = make_queue<Q>();
    std::uint64_t pushed = 0;

    for (auto _: state) {
        state.PauseTiming();
        set_occupancy(*q, start);
        state.ResumeTiming();

        while (q->push(Q::sample())) pushed++;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(pushed));
}

/// From `fill_pct` full down to empty: the cost of a consumer catching up on a
/// backlog, with every slot released back to the producer on the way.
template<bench_queue Q>
void BM_Drain(benchmark::State &state) {
    const std::size_t start = occupancy(Q::capacity, state.range(0));

    auto                   q      = make_queue<Q>();
    std::uint64_t          popped = 0;
    typename Q::value_type out;

    for (auto _: state) {
        state.PauseTiming();
        set_occupancy(*q, start);
        state.ResumeTiming();

        while (q->pop(out)) popped++;
    }
    benchmark::DoNotOptimize(out);
    state.SetItemsProcessed(static_cast<std::int64_t>(popped));
}

/// A full sweep of the ring and back to where it started, so no setup is needed
/// between iterations: the bulk transfer cost, rather than the single-slot cost.
template<bench_queue Q>
void BM_FillAndDrain(benchmark::State &state) {
    const std::size_t start = occupancy(Q::capacity, state.range(0));

    auto q = make_queue<Q>();
    set_occupancy(*q, start);

    std::uint64_t          moved = 0;
    typename Q::value_type out;

    for (auto _: state) {
        std::size_t filled = 0;
        while (q->push(Q::sample())) filled++;
        for (std::size_t i = 0; i < filled; i++) q->pop(out);
        moved += 2 * filled;
    }
    benchmark::DoNotOptimize(out);
    state.SetItemsProcessed(static_cast<std::int64_t>(moved));
}

/// Steady state at a fixed occupancy: one push and one pop per element, with the
/// queue neither growing nor shrinking. `fill_pct` decides whether the producer
/// and consumer are working on the same cache line (empty) or a ring apart (full).
template<bench_queue Q>
void BM_PushPop(benchmark::State &state) {
    constexpr std::size_t batch = 10'000;
    // At exactly full every push would be rejected, so hold one slot open.
    const std::size_t start = std::min(occupancy(Q::capacity, state.range(0)), Q::capacity - 1);

    auto q = make_queue<Q>();
    set_occupancy(*q, start);

    typename Q::value_type out;

    for (auto _: state) {
        for (std::size_t i = 0; i < batch; i++) {
            q->push(Q::sample());
            q->pop(out);
        }
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * batch));
}

// -- Producer / consumer throughput ------------------------------------------

/**
 * How long `elements` take to be produced and drained again by `producers`
 * pushing threads against `consumers` popping threads, starting from a queue
 * that is `fill_pct` full.
 *
 * Every iteration is one complete run: the producers share the element budget
 * between them, the consumers drain until nothing the run produced (nor anything
 * it started with) is left, and the queue is empty again at the end. Timing is
 * real time, since the measurement is how long the whole set takes, not how much
 * CPU the threads burned spinning on a full or empty queue.
 */
template<bench_queue Q>
void BM_Throughput(benchmark::State &state) {
    using value_type = typename Q::value_type;

    const auto producers = static_cast<std::size_t>(state.range(0));
    const auto consumers = static_cast<std::size_t>(state.range(1));
    const auto start     = occupancy(Q::capacity, state.range(2));
    const auto elements  = static_cast<std::uint64_t>(state.range(3));

    const std::uint64_t target = elements + start;
    std::uint64_t       moved  = 0;

    for (auto _: state) {
        state.PauseTiming();

        auto q = make_queue<Q>();
        set_occupancy(*q, start);

        std::atomic<std::uint64_t> consumed{0};
        std::atomic_bool           go{false};

        std::vector<std::thread> workers;
        workers.reserve(producers + consumers);

        for (std::size_t i = 0; i < producers; i++) {
            // The first producer takes the remainder so the budget is exact.
            const std::uint64_t share = elements / producers + (i == 0 ? elements % producers : 0);
            workers.emplace_back([&, share] {
                go.wait(false, std::memory_order_acquire);
                for (std::uint64_t n = 0; n < share; n++) {
                    while (!q->push(Q::sample())) {}
                }
            });
        }
        for (std::size_t i = 0; i < consumers; i++) {
            workers.emplace_back([&] {
                go.wait(false, std::memory_order_acquire);
                value_type out;
                while (consumed.load(std::memory_order_relaxed) < target) {
                    if (q->pop(out)) consumed.fetch_add(1, std::memory_order_relaxed);
                }
                benchmark::DoNotOptimize(out);
            });
        }

        state.ResumeTiming();

        go.store(true, std::memory_order_release);
        go.notify_all();
        for (auto &w: workers) w.join();

        moved += target;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(moved));
}

// -- Argument sets -----------------------------------------------------------

using bench_ptr = benchmark::internal::Benchmark *;

/// Elements produced and drained per BM_Throughput iteration.
constexpr std::int64_t elements_per_run = 100'000;

void throughput_args(const bench_ptr b, const std::initializer_list<std::pair<std::int64_t, std::int64_t>> shapes) {
    b->ArgNames({"producers", "consumers", "fill_pct", "elements"});
    for (const auto &[producers, consumers]: shapes) {
        b->Args({producers, consumers, 0, elements_per_run});
    }
}

/// The one shape every queue must support, across the three starting points.
void spsc_shapes(const bench_ptr b) {
    b->ArgNames({"producers", "consumers", "fill_pct", "elements"});
    for (const std::int64_t fill: {0, 50, 100}) b->Args({1, 1, fill, elements_per_run});
}

void mpsc_shapes(const bench_ptr b) { throughput_args(b, {{1, 1}, {2, 1}, {4, 1}, {8, 1}}); }
void spmc_shapes(const bench_ptr b) { throughput_args(b, {{1, 1}, {1, 2}, {1, 4}, {1, 8}}); }
void mpmc_shapes(const bench_ptr b) { throughput_args(b, {{1, 1}, {2, 2}, {4, 4}, {8, 8}, {4, 1}, {1, 4}}); }

// -- Registration ------------------------------------------------------------

#define DSL_BQ_TYPE(Payload, Model, Sizing, Capacity)                                    \
    dcl::bench::queue_under_test<dcl::bench::Payload, dcl::concurrency::Model,           \
                                 dcl::bench::sizing::Sizing, Capacity>

/// Registers one (workload, payload, model, sizing, capacity) point. `Options` is
/// a chain of google-benchmark modifiers -- `->Arg(0)`, `->Apply(shapes)`, ...
#define DSL_BQ_ONE(Workload, Payload, Model, Sizing, Capacity, Options)                            \
    BENCHMARK_TEMPLATE(Workload, DSL_BQ_TYPE(Payload, Model, Sizing, Capacity))                    \
        ->Name(dcl::bench::bench_name<DSL_BQ_TYPE(Payload, Model, Sizing, Capacity)>(#Workload))   \
            Options;

/// Both sizings of one model.
#define DSL_BQ_SIZINGS(Workload, Payload, Model, Capacity, Options)          \
    DSL_BQ_ONE(Workload, Payload, Model, fixed, Capacity, Options)           \
    DSL_BQ_ONE(Workload, Payload, Model, dynamic, Capacity, Options)

/// Every payload, for one model.
#define DSL_BQ_PAYLOADS(Workload, Model, Capacity, Options)                   \
    DSL_BQ_SIZINGS(Workload, int_payload, Model, Capacity, Options)           \
    DSL_BQ_SIZINGS(Workload, string_payload, Model, Capacity, Options)        \
    DSL_BQ_SIZINGS(Workload, complex_payload, Model, Capacity, Options)

/// Every model, every sizing, every payload.
#define DSL_BQ_ALL(Workload, Capacity, Options)               \
    DSL_BQ_PAYLOADS(Workload, SPSC, Capacity, Options)        \
    DSL_BQ_PAYLOADS(Workload, SPMC, Capacity, Options)        \
    DSL_BQ_PAYLOADS(Workload, MPSC, Capacity, Options)        \
    DSL_BQ_PAYLOADS(Workload, MPMC, Capacity, Options)

// Filling from new and from half full; draining a full and a half-full queue.
DSL_BQ_ALL(BM_Fill, 1024, ->ArgName("fill_pct")->Arg(0)->Arg(50)->MinWarmUpTime(0.5))
DSL_BQ_ALL(BM_Drain, 1024, ->ArgName("fill_pct")->Arg(100)->Arg(50)->MinWarmUpTime(0.5))

// The bulk sweep, over three capacities: in cache, around the L2 boundary, and
// beyond it.
DSL_BQ_ALL(BM_FillAndDrain, 64, ->ArgName("fill_pct")->Arg(0)->MinWarmUpTime(0.5))
DSL_BQ_ALL(BM_FillAndDrain, 1024, ->ArgName("fill_pct")->Arg(0)->Arg(50)->MinWarmUpTime(0.5))
DSL_BQ_ALL(BM_FillAndDrain, 16384, ->ArgName("fill_pct")->Arg(0)->MinWarmUpTime(0.5))

// Steady state at an empty, a half full, and a full queue.
DSL_BQ_ALL(BM_PushPop, 1024, ->ArgName("fill_pct")->Arg(0)->Arg(50)->Arg(100)->MinWarmUpTime(0.5))

// Throughput under the thread shapes each model is meant to serve. SPMC and MPSC
// currently select the MPMC algorithm, so for now their numbers measure it under
// their own shape rather than a dedicated implementation.
DSL_BQ_PAYLOADS(BM_Throughput, SPSC, 1024, ->Apply(dcl::bench::spsc_shapes)->UseRealTime())
DSL_BQ_PAYLOADS(BM_Throughput, SPMC, 1024, ->Apply(dcl::bench::spmc_shapes)->UseRealTime())
DSL_BQ_PAYLOADS(BM_Throughput, MPSC, 1024, ->Apply(dcl::bench::mpsc_shapes)->UseRealTime())
DSL_BQ_PAYLOADS(BM_Throughput, MPMC, 1024, ->Apply(dcl::bench::mpmc_shapes)->UseRealTime())

// How much the ring size matters once several threads are contending for it.
DSL_BQ_PAYLOADS(BM_Throughput, MPMC, 64, ->Apply(dcl::bench::mpmc_shapes)->UseRealTime())
DSL_BQ_PAYLOADS(BM_Throughput, MPMC, 16384, ->Apply(dcl::bench::mpmc_shapes)->UseRealTime())

} // namespace
} // namespace dcl::bench
