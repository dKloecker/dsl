function(dsl_add_benchmark_suite group)
    file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${group}/*.bench.cpp")
    add_executable(${group}_benchmarks ${sources})
    target_link_libraries(${group}_benchmarks PRIVATE dsl::${group} benchmark::benchmark_main)
    target_compile_options(${group}_benchmarks PRIVATE -O3 -march=native)
endfunction()

dsl_add_benchmark_suite(dsl)
dsl_add_benchmark_suite(dcl)
dsl_add_benchmark_suite(dal)

# The queue comparison benchmarks measure against boost::lockfree::spsc_queue and boost::lockfree::queue.
find_package(Boost CONFIG REQUIRED)
target_link_libraries(dcl_benchmarks PRIVATE Boost::headers)
