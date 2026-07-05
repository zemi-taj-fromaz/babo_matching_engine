# --- liquibook_ref ---------------------------------------------------------
# Reference matching engine, vendored from the dendisuhubdy/liquibook fork.
#
# NOTE: liquibook's actual engine (OrderBook<>, DepthOrderBook<>) is header-only
# templates under include/book + include/simple (+ include/latency, a header-only
# clock_gettime shim for per-order timing on MSVC). The templates get instantiated
# inside whatever consumes them (the bench target), so this static lib itself is
# tiny: it only carries the one concrete translation unit, simple_order.cpp
# (the SimpleOrder type used to drive the book).
#
# This is a frozen baseline for benchmarking -- intentionally vendored in-tree
# (not FetchContent) so its numbers never shift under us. Not upstream-synced.

add_library(liquibook_ref STATIC src/simple_order.cpp)
set_target_properties(liquibook_ref PROPERTIES POSITION_INDEPENDENT_CODE ON)

# PUBLIC: consumers need these headers to instantiate the templates.
target_include_directories(liquibook_ref PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# liquibook targets C++11; it compiles fine under our C++20.
target_compile_features(liquibook_ref PUBLIC cxx_std_20)

# Suppress the one-time std::cerr deprecation warnings liquibook prints from
# header code, so they don't pollute benchmark output. Affects only logging.
target_compile_definitions(liquibook_ref PUBLIC LIQUIBOOK_IGNORES_DEPRECATED_CALLS)
