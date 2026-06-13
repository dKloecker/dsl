set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

cmake_minimum_required(VERSION 3.26)
project(dsl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DSL_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(DSL_BUILD_BENCHMARKS "Build benchmarks" ${PROJECT_IS_TOP_LEVEL})

add_library(dsl STATIC
        dsl/logging/dsl_async_logger.cpp
        dsl/logging/dsl_logger_utils.cpp
)
target_include_directories(dsl PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# The logging headers expose a few test-only hooks behind #ifdef TESTING
if (DSL_BUILD_TESTING)
    target_compile_definitions(dsl PRIVATE TESTING)
endif ()

if (DSL_BUILD_TESTING)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)
    add_subdirectory(tests)
endif ()

if (DSL_BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
    add_subdirectory(benchmarks)
endif ()
