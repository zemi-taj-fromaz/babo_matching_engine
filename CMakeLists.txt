cmake_minimum_required(VERSION 3.23)
project(BaboMatchingEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

enable_testing()
include(CTest)

# Fix for Boost/Winsock conflicts in tests
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.x
)

# Force static gtest/gmock — building them as DLLs with -fsanitize=undefined embeds
# the UBSan standalone runtime inside libgtest.dll, causing duplicate symbol errors
# when libgmock.dll then also pulls in libclang_rt.ubsan_standalone.a.
#set(BUILD_SHARED_LIBS OFF)
#set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

message(STATUS "PROJECT NAME  ${PROJECT_NAME}")
message(STATUS "CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE}")

# --- Compiler / optimization policy (all in cmake/CompilerSettings.cmake) ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerSettings)

# --- Build options ----------------------------------------------------------
# The default build has one canonical babobook configuration: pull-based depth
# enabled and the default PIN capacity (64). Optional compile flags may add
# explicitly requested experiment targets, but the default never builds a matrix
# of babobook variants.
option(BABO_BUILD_PIN_SWEEP "Also build the non-default pin_node capacities (babo_cap{16,32,128}_perf)" OFF)

# --- Static libraries to compare ---
# liquibook_ref: vendored reference engine (in-tree, frozen baseline).
# babo_book:     your own engine (you'll be writing this).
add_subdirectory(libs/liquibook)
add_subdirectory(libs/babobook)
add_subdirectory(test/babo_test)
add_subdirectory(test/liqui_test)
add_subdirectory(benchmark)

# --- Order-book perf executables ---
# Defined in perf/CMakeLists.txt; added here after the engine libraries so the
# perf targets can link liquibook_ref / babobook.
add_subdirectory(perf)
# --- Benchmark / comparison harness ---
#add_subdirectory(bench)
