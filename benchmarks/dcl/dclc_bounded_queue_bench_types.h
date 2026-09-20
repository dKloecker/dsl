//
// Created by Dominic Kloecker on 20/09/2026.
//
// Shared fixtures for the bounded queue comparison suites:
//
// Benchmarks are named "<workload>/<queue>/<payload>/<capacity>", so a single
// implementation or workload can be picked out with e.g.
//   ./dcl_benchmarks --benchmark_filter='PushPop/dsl\.'
//
#ifndef DSL_BENCHMARKS_DCLC_BOUNDED_QUEUE_BENCH_TYPES_H_
#define DSL_BENCHMARKS_DCLC_BOUNDED_QUEUE_BENCH_TYPES_H_

#include <array>
#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>

#include <benchmark/benchmark.h>
#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include "dclc_bounded_queue.h"

namespace dcl::bench {

// -- Payloads ----------------------------------------------------------------
//
// A payload is any queue element that carries a `name` for the benchmark label.

template<typename T>
concept bench_payload = std::default_initializable<T> && requires {
    { T::name } -> std::convertible_to<const char *>;
};

/// Word-sized element: the queue's own bookkeeping dominates the measurement.
struct SimpleObject {
    static constexpr auto name = "simple";
    int someValue = 42;
};

/// Half-kilobyte element: moving the payload dominates the measurement.
struct ComplexObject {
    static constexpr auto name = "complex";

    int                        someValue1 = 1;
    int                        someValue2 = 2;
    int                        someValue3 = 3;
    std::array<std::byte, 256> data1{};
    std::array<std::byte, 256> data2{};
};

// -- Adapters ----------------------------------------------------------------

/**
 * Storage plus the uniform surface every workload is written against.
 * The queues under comparison disagree on how capacity is supplied and on what else they offer
 *
 * `Capacity` is the capacity the adapter was asked for: fixed-size queues bake
 * it into their type, runtime-sized ones take it as a constructor argument.
 */
template<bench_payload T, std::size_t Capacity, typename Queue>
struct queue_adapter {
    using value_type = T;
    using queue_type = Queue;

    static constexpr std::size_t capacity = Capacity;

    Queue q;

    queue_adapter() requires std::default_initializable<Queue> = default;

    explicit queue_adapter(const std::size_t runtime_capacity)
        : q(runtime_capacity) {}

    bool push(const T &in) { return q.push(in); }
    bool pop(T &out) { return q.pop(out); }
};

/// What the workloads require of anything registered in a queue list.
template<typename A>
concept bench_queue = bench_payload<typename A::value_type> &&
                      requires(A a, const typename A::value_type &in, typename A::value_type &out) {
                          { a.push(in) } -> std::same_as<bool>;
                          { a.pop(out) } -> std::same_as<bool>;
                          { A::name } -> std::convertible_to<const char *>;
                          { A::capacity } -> std::convertible_to<std::size_t>;
                      };

// dcl::bounded_queue, one adapter per concurrency model

template<bench_payload T, std::size_t Capacity>
struct dsl_spsc_fixed : queue_adapter<T, Capacity, dcl::b_spsc_q<T, Capacity>> {
    static constexpr const char *name = "dsl.spsc.fixed";
};

template<bench_payload T, std::size_t Capacity>
struct dsl_spsc_dyn : queue_adapter<T, Capacity, dcl::b_spsc_q<T>> {
    using base                        = queue_adapter<T, Capacity, dcl::b_spsc_q<T>>;
    static constexpr const char *name = "dsl.spsc.dyn";

    dsl_spsc_dyn()
        : base(Capacity) {}
};

template<bench_payload T, std::size_t Capacity>
struct dsl_spmc_fixed : queue_adapter<T, Capacity, dcl::b_spmc_q<T, Capacity>> {
    static constexpr const char *name = "dsl.spmc.fixed";
};

template<bench_payload T, std::size_t Capacity>
struct dsl_spmc_dyn : queue_adapter<T, Capacity, dcl::b_spmc_q<T>> {
    using base                        = queue_adapter<T, Capacity, dcl::b_spmc_q<T>>;
    static constexpr const char *name = "dsl.spmc.dyn";

    dsl_spmc_dyn()
        : base(Capacity) {}
};

template<bench_payload T, std::size_t Capacity>
struct dsl_mpsc_fixed : queue_adapter<T, Capacity, dcl::b_mpsc_q<T, Capacity>> {
    static constexpr const char *name = "dsl.mpsc.fixed";
};

template<bench_payload T, std::size_t Capacity>
struct dsl_mpsc_dyn : queue_adapter<T, Capacity, dcl::b_mpsc_q<T>> {
    using base                        = queue_adapter<T, Capacity, dcl::b_mpsc_q<T>>;
    static constexpr const char *name = "dsl.mpsc.dyn";

    dsl_mpsc_dyn()
        : base(Capacity) {}
};

template<bench_payload T, std::size_t Capacity>
struct dsl_mpmc_fixed : queue_adapter<T, Capacity, dcl::b_mpmc_q<T, Capacity>> {
    static constexpr const char *name = "dsl.mpmc.fixed";
};

template<bench_payload T, std::size_t Capacity>
struct dsl_mpmc_dyn : queue_adapter<T, Capacity, dcl::b_mpmc_q<T>> {
    using base                        = queue_adapter<T, Capacity, dcl::b_mpmc_q<T>>;
    static constexpr const char *name = "dsl.mpmc.dyn";

    dsl_mpmc_dyn()
        : base(Capacity) {}
};

// boost::lockfree, as the external reference point.

template<bench_payload T, std::size_t Capacity>
struct boost_spsc_fixed
    : queue_adapter<T, Capacity, boost::lockfree::spsc_queue<T, boost::lockfree::capacity<Capacity>>> {
    static constexpr const char *name = "boost.spsc.fixed";
};

template<bench_payload T, std::size_t Capacity>
struct boost_spsc_dyn : queue_adapter<T, Capacity, boost::lockfree::spsc_queue<T>> {
    using base                        = queue_adapter<T, Capacity, boost::lockfree::spsc_queue<T>>;
    static constexpr const char *name = "boost.spsc.dyn";

    boost_spsc_dyn()
        : base(Capacity) {}
};

template<bench_payload T, std::size_t Capacity>
struct boost_mpmc : queue_adapter<T, Capacity, boost::lockfree::queue<T, boost::lockfree::capacity<Capacity>>> {
    static constexpr const char *name = "boost.mpmc";
};

/// Naming
/// "<workload>/<queue>/<payload>/<capacity>" -- see the filter note at the top.
template<bench_queue Adapter>
std::string bench_name(const std::string_view workload) {
    return std::string{workload}
           .append("/")
           .append(Adapter::name)
           .append("/")
           .append(Adapter::value_type::name)
           .append("/")
           .append(std::to_string(Adapter::capacity));
}

} // namespace dcl::bench

/// Register

/**
 * Registers one (workload, queue, payload, capacity) point. `Options` is a chain
 * of google-benchmark modifiers applied to it -- `->MinWarmUpTime(1.0)`, or
 * `->Args({4, 4})->UseRealTime()`, or nothing at all.
 */
#define DSL_BENCH_ONE(Workload, Adapter, Payload, Capacity, Options)          \
    BENCHMARK_TEMPLATE(Workload, Adapter<Payload, Capacity>)                  \
        ->Name(dcl::bench::bench_name<Adapter<Payload, Capacity>>(#Workload)) \
            Options;

/// One queue, every payload.
#define DSL_BENCH_PAYLOADS(Workload, Adapter, Capacity, Options)                  \
    DSL_BENCH_ONE(Workload, Adapter, dcl::bench::SimpleObject, Capacity, Options) \
    DSL_BENCH_ONE(Workload, Adapter, dcl::bench::ComplexObject, Capacity, Options)

/**
 * Every queue that can be driven by one producer and one consumer  which is
 * every queue here, so this list doubles as the roster. Add a queue here first.
 */
#define DSL_BENCH_ALL_SPSC(Workload, Capacity, Options)                           \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_spsc_fixed, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_spsc_dyn, Capacity, Options)     \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_spmc_fixed, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_spmc_dyn, Capacity, Options)     \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpsc_fixed, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpsc_dyn, Capacity, Options)     \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpmc_fixed, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpmc_dyn, Capacity, Options)     \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::boost_spsc_fixed, Capacity, Options) \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::boost_spsc_dyn, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::boost_mpmc, Capacity, Options)

/**
 * Every queue that tolerates several producers against a single consumer: the
 * dedicated MPSC algorithm, and the MPMC ones that also cover the shape.
 */
#define DSL_BENCH_ALL_MPSC(Workload, Capacity, Options)                         \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpsc_fixed, Capacity, Options) \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpsc_dyn, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpmc_fixed, Capacity, Options) \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpmc_dyn, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::boost_mpmc, Capacity, Options)

/**
 * Every queue that tolerates a single producer against several consumers: the
 * dedicated SPMC algorithm, and the MPMC ones that also cover the shape.
 */
#define DSL_BENCH_ALL_SPMC(Workload, Capacity, Options)                         \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_spmc_fixed, Capacity, Options) \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_spmc_dyn, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpmc_fixed, Capacity, Options) \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpmc_dyn, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::boost_mpmc, Capacity, Options)

/// Every queue that tolerates several producers *and* several consumers.
#define DSL_BENCH_ALL_MPMC(Workload, Capacity, Options)                         \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpmc_fixed, Capacity, Options) \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::dsl_mpmc_dyn, Capacity, Options)   \
    DSL_BENCH_PAYLOADS(Workload, dcl::bench::boost_mpmc, Capacity, Options)

#endif // DSL_BENCHMARKS_DCLC_BOUNDED_QUEUE_BENCH_TYPES_H_
