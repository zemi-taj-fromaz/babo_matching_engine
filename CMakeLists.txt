cmake_minimum_required(VERSION 3.23)
project(BaboMatchingEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Spdlog ---
message(STATUS "---- Spdlog ----")
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_ENABLE_PCH OFF CACHE BOOL "" FORCE)
# OFF so spdlog bundles 'fmt' instead of using std::format
set(SPDLOG_USE_STD_FORMAT OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.17.0
        GIT_SHALLOW    TRUE
        SYSTEM
)
FetchContent_MakeAvailable(spdlog)

add_executable(babo_matching_engine)
target_sources(babo_matching_engine
        PRIVATE
            src/main.cpp
)

target_link_libraries(babo_matching_engine PRIVATE spdlog::spdlog)

# --- Precompiled header ---
# spdlog must be linked BEFORE this line so its include dir is known when
# the PCH (which includes <spdlog/spdlog.h>) is compiled.
target_precompile_headers(babo_matching_engine PRIVATE src/pch.hpp)


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

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

if (IPO_SUPPORTED)
    message(STATUS "IPO / LTO enabled")
else ()
    message(STATUS "IPO / LTO not supported: <${IPO_ERROR}>")
endif ()

# --- Static libraries to compare ---
# liquibook_ref: vendored reference engine (in-tree, frozen baseline).
# babo_book:     your own engine (you'll be writing this).
add_subdirectory(libs/liquibook)
add_subdirectory(libs/babobook)
add_subdirectory(test/babo_test)
add_subdirectory(test/liqui_test)
add_subdirectory(benchmark)

# --- Order-book benchmark executables ---
# Defined in src/CMakeLists.txt; added here (after the engine libraries) so the
# bench targets can link liquibook_ref / babobook.
add_subdirectory(src)
# --- Benchmark / comparison harness ---
#add_subdirectory(bench)
