dsl_add_library(dal PACKAGES dall DEPENDS dsl::dcl)

# AsyncLogger is a template with explicit instantiations in its .cpp, and the
# <4, 32> instantiation used by the tests exists only under DSL_TESTING. The
# definition must therefore be PUBLIC: the library, the tests and the
# benchmarks all have to agree on it or the test build fails to link.
if (DSL_BUILD_TESTING)
    target_compile_definitions(dal PUBLIC DSL_TESTING)
endif ()
