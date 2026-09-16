cmake_minimum_required(VERSION 3.26)

# Resolve the vcpkg toolchain before project() so the build works from any entry
# point: a CLion profile, a bare `cmake -S . -B build`, or a vcpkg port build
# (which passes its own toolchain and therefore skips this block).
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if (DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain")
    elseif (EXISTS "$ENV{HOME}/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain")
    endif ()
endif ()

# vcpkg's toolchain sets CMAKE_SYSTEM_NAME explicitly, which makes CMake pin the
# dependency builds to the *host* macOS version. A plain build here leaves the
# target unset and gets the compiler default instead (major.0), so every link
# warns that gtest/benchmark were "built for newer macOS". Track the host the
# same way vcpkg does and the two always agree -- across OS upgrades too, since
# nothing is written down. An explicit -DCMAKE_OSX_DEPLOYMENT_TARGET still wins.
if (APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    execute_process(COMMAND sw_vers -productVersion
            OUTPUT_VARIABLE DSL_HOST_MACOS_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${DSL_HOST_MACOS_VERSION}"
            CACHE STRING "macOS deployment target (defaults to the host version)")
endif ()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" DSL_VCPKG_MANIFEST)
string(JSON DSL_VERSION GET "${DSL_VCPKG_MANIFEST}" version)

project(dsl VERSION ${DSL_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DSL_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(DSL_BUILD_BENCHMARKS "Build benchmarks" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)
include(cmake/dsl_add_library.cmake)

add_subdirectory(src/dsl)
add_subdirectory(src/dcl)
add_subdirectory(src/dal)

# Umbrella target for consumers that want everything.
add_library(dsl_all INTERFACE)
add_library(dsl::all ALIAS dsl_all)

target_link_libraries(dsl_all INTERFACE
        dsl::dsl
        dsl::dcl
        dsl::dal
)

set_target_properties(dsl_all PROPERTIES EXPORT_NAME all)
install(TARGETS dsl_all EXPORT dslTargets)

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

include(CMakePackageConfigHelpers)

install(EXPORT dslTargets
        FILE dslTargets.cmake
        NAMESPACE dsl::
        DESTINATION "${CMAKE_INSTALL_DATADIR}/dsl")

configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dslConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/dslConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_DATADIR}/dsl")

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/dslConfigVersion.cmake"
        COMPATIBILITY SameMajorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/dslConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/dslConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/dsl")
