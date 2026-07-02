
cmake_minimum_required(VERSION 3.23)
project(BaboMatchingEngine)

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

# --- Static libraries to compare ---
# liquibook_ref: vendored reference engine (in-tree, frozen baseline).
# babo_book:     your own engine (you'll be writing this).
add_subdirectory(libs/liquibook)
add_subdirectory(libs/babobook)

# --- Benchmark / comparison harness ---
#add_subdirectory(bench)
