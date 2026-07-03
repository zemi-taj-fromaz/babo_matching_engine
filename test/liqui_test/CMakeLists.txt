# liqui_test: the upstream liquibook unit tests, run against the vendored
# liquibook_ref engine. Test sources are copied verbatim from upstream; only the
# Boost.Test header block is swapped for unit/boost_shim.h (Boost -> GoogleTest),
# since this project builds with GoogleTest and has no Boost dependency.
file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS "*.h" "*.cpp")

add_executable(liqui_unit ${TEST_FILES})
target_link_libraries(liqui_unit PRIVATE liquibook_ref GTest::gtest_main)
target_include_directories(liqui_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(liqui_unit
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 30
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 900       # 15 minutes - increased for slow CI/CD machines
)
