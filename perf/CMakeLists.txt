# --- Order-book perf executables --------------------------------------------
# Standalone binaries for hardware-counter profiling and latency checks.
# Each target links only the engine it exercises. Console formatting is a
# dependency-free header (perf/bench_log.h, ANSI colors).
#
# Depth ON vs OFF is a compile-time toggle, so each engine ships two binaries
# from the SAME source:
#   <engine>_perf        -> depth OFF  (lean; the fair head-to-head baseline)
#   <engine>_depth_perf  -> depth ON   (stock behaviour: maintains aggregate depth)
# babo toggles on BABO_NO_DEPTH; liquibook on LIQUI_NO_DEPTH (see
# benchmark/adapters/liqui_book_type.h, which liqui_perf shares with the adapter).

set(ADAPTERS_DIR ${CMAKE_SOURCE_DIR}/benchmark/adapters)   # for liqui_book_type.h

# helper: one perf executable = source + engine lib, with an optional depth macro
# and (for liquibook) the shared adapter include dir.
function(add_perf target source engine_lib)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${engine_lib})
    target_include_directories(${target} PRIVATE ${ADAPTERS_DIR})
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(ARGN)
        target_compile_definitions(${target} PRIVATE ${ARGN})
    endif()
endfunction()

# --- liquibook --------------------------------------------------------------
add_perf(liqui_perf       liqui_perf_order_book.cpp  liquibook_ref  LIQUI_NO_DEPTH)  # depth OFF
add_perf(liqui_depth_perf liqui_perf_order_book.cpp  liquibook_ref)                  # depth ON

# --- babo -------------------------------------------------------------------
add_perf(babo_perf        babo_perf_order_book.cpp   babobook  BABO_NO_DEPTH)        # depth OFF (pin=64)
add_perf(babo_depth_perf  babo_perf_order_book.cpp   babobook)                       # depth ON

# pin_node capacity sweep (depth OFF, to isolate the core structure): builds one
# babo_cap<N>_perf per capacity. Run them and compare THROUGHPUT to see how orders
# per PIN node affects cache behaviour. NB: babo_cap64_perf == the default babo_perf.
foreach(CAP IN ITEMS 16 32 64 128)
    add_perf(babo_cap${CAP}_perf babo_perf_order_book.cpp babobook
             BABO_NO_DEPTH BABO_PIN_CAPACITY=${CAP})
endforeach()
