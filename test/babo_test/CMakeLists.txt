# babo_test: unit + functional tests for the babo matching engine.
#
# Convention (mirrors liquibook): translated liquibook scenarios live in ut_*.cpp
# at this level; shared helpers live in unit/. For now everything builds into a
# single babo_unit binary; split functional tests out once they exist.
file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS "*.h" "*.cpp")

add_executable(babo_unit ${TEST_FILES})
target_link_libraries(babo_unit PRIVATE babobook::babobook GTest::gtest_main)
target_include_directories(babo_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(babo_unit
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 30
        PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 900       # 15 minutes - increased for slow CI/CD machines
)
