# Tests live outside the dsl/ source tree so the published include tree ships
# only library headers. Grouped one executable per component; the TESTING
# definition matches the test-only hooks in the dsl headers/sources.
function(dsl_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dsl GTest::gtest_main)
    target_compile_definitions(${name} PRIVATE TESTING)
    gtest_discover_tests(${name})
endfunction()

dsl_add_test(core_tests
        core/pool_resource.t.cpp
        core/fixed_size_pool_resource.t.cpp
        core/spsc_queue.t.cpp
)

dsl_add_test(logging_tests
        logging/async_logger.t.cpp
        logging/log_writer.t.cpp
)
