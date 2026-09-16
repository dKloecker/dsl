# One test executable per sub-library, sources globbed from tests/<group>/.
# Adding a test means dropping a <component>.t.cpp into the right folder.
function(dsl_add_test_suite group)
    file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${group}/*.t.cpp")
    add_executable(${group}_tests ${sources})
    target_link_libraries(${group}_tests PRIVATE dsl::${group} GTest::gtest_main)
    gtest_discover_tests(${group}_tests)
endfunction()

dsl_add_test_suite(dsl)
dsl_add_test_suite(dcl)
dsl_add_test_suite(dal)
