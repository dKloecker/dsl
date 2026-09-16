# One rule for every sub-library in this repo.
#
#   dsl_add_library(<group> PACKAGES <pkg>... [DEPENDS <target>...])
#
# A group lives in src/<group>/ and holds one directory per package. Every
# package directory is added to the include path *directly*, so headers are
# included by bare filename -- #include "dclc_spsc_bounded_queue.h" -- both
# while building here and from the installed tree, where the headers are
# flattened into <prefix>/include.
#
# Sources are globbed: adding a component means adding its .h/.cpp pair and
# nothing else. CONFIGURE_DEPENDS makes CMake re-check the glob each build.
function(dsl_add_library group)
    cmake_parse_arguments(ARG "" "" "PACKAGES;DEPENDS" ${ARGN})

    set(sources "")
    foreach (pkg IN LISTS ARG_PACKAGES)
        file(GLOB pkg_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${pkg}/*.cpp")
        list(APPEND sources ${pkg_sources})
    endforeach ()

    add_library(${group} STATIC ${sources})
    add_library(dsl::${group} ALIAS ${group})

    # Consumers must compile as C++23 or newer; carried by the exported target.
    target_compile_features(${group} PUBLIC cxx_std_23)
    target_link_libraries(${group} PUBLIC ${ARG_DEPENDS})

    foreach (pkg IN LISTS ARG_PACKAGES)
        target_include_directories(${group} PUBLIC
                "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${pkg}>")
        install(DIRECTORY "${pkg}/"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
                FILES_MATCHING PATTERN "*.h")
    endforeach ()
    target_include_directories(${group} PUBLIC
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    install(TARGETS ${group} EXPORT dslTargets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endfunction()
