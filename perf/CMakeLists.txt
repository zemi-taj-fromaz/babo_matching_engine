# --- Order-book perf executables --------------------------------------------
# Standalone binaries for hardware-counter profiling and latency checks.
# Each target links only the engine it exercises.
#
# Depth ON vs OFF is a compile-time toggle, so each engine ships two binaries
# from the SAME source:
#   <engine>_perf        -> depth OFF  (lean; the fair head-to-head baseline)
#   <engine>_depth_perf  -> depth ON   (stock behaviour: maintains aggregate depth)
# babo toggles on BABO_NO_DEPTH; liquibook on LIQUI_NO_DEPTH (see
# benchmark/adapters/liqui_book_type.h, which liqui_perf shares with the adapter).

set(ADAPTERS_DIR ${CMAKE_SOURCE_DIR}/benchmark/adapters)   # for liqui_book_type.h

# --- liquibook --------------------------------------------------------------
add_executable(liqui_perf liqui_perf_order_book.cpp)
target_link_libraries(liqui_perf PRIVATE liquibook_ref)
target_include_directories(liqui_perf PRIVATE ${ADAPTERS_DIR})
target_compile_definitions(liqui_perf PRIVATE LIQUI_NO_DEPTH)   # depth OFF
target_compile_features(liqui_perf PRIVATE cxx_std_20)

add_executable(liqui_depth_perf liqui_perf_order_book.cpp)
target_link_libraries(liqui_depth_perf PRIVATE liquibook_ref)
target_include_directories(liqui_depth_perf PRIVATE ${ADAPTERS_DIR})
target_compile_features(liqui_depth_perf PRIVATE cxx_std_20)     # depth ON (no macro)

# --- babo -------------------------------------------------------------------
add_executable(babo_perf babo_perf_order_book.cpp)
target_link_libraries(babo_perf PRIVATE babobook)
target_compile_definitions(babo_perf PRIVATE BABO_NO_DEPTH)     # depth OFF
target_compile_features(babo_perf PRIVATE cxx_std_20)

add_executable(babo_depth_perf babo_perf_order_book.cpp)
target_link_libraries(babo_depth_perf PRIVATE babobook)
target_compile_features(babo_depth_perf PRIVATE cxx_std_20)      # depth ON (no macro)
