function(dsl_add_benchmark name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dsl benchmark::benchmark_main)
    target_compile_options(${name} PRIVATE -O3 -march=native)
endfunction()

# Core Components
dsl_add_benchmark(memory_benchmarks core/fixed_size_pool_resource.bench.cpp)
dsl_add_benchmark(memory_comparison_benchmarks core/fixed_size_pool_resource_comparison.bench.cpp)
dsl_add_benchmark(spsc_queue_benchmarks core/spsc_queue.bench.cpp)

# Include BOOST for SPCC_QUEUE comparison benchmark
find_package(Boost REQUIRED)
dsl_add_benchmark(spsc_queue_comparison_benchmarks core/spsc_queue_comparison.bench.cpp)
target_link_libraries(spsc_queue_comparison_benchmarks PRIVATE Boost::headers)

# Logging Components
dsl_add_benchmark(logging_benchmarks logging/async_logger.bench.cpp)
target_compile_definitions(logging_benchmarks PRIVATE TESTING)
