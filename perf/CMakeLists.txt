# --- Order-book perf executables --------------------------------------------
# Standalone binaries for hardware-counter profiling and latency checks.
# Each target links only the engine it exercises. Console formatting is a
# dependency-free header (perf/bench_log.h, ANSI colors).
#
# Depth is ON by default (babo derives it for ~free). The depth-OFF variants and
# the pin_node capacity sweep are opt-in (keeps the default build lean):
#   -DBABO_BUILD_NODEPTH=ON    -> babo_nodepth_perf, liqui_nodepth_perf
#   -DBABO_BUILD_PIN_SWEEP=ON  -> babo_cap{16,32,64,128}_perf
# babo toggles depth on BABO_NO_DEPTH; liquibook on LIQUI_NO_DEPTH (see
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

# --- default: depth ON ------------------------------------------------------
add_perf(liqui_perf  liqui_perf_order_book.cpp  liquibook_ref)   # depth ON
add_perf(babo_perf   babo_perf_order_book.cpp   babobook)        # depth ON

# --- opt-in: pin_node capacity sweep (depth ON) -----------------------------
# One babo_cap<N>_perf per capacity. Run them and compare THROUGHPUT to see how
# orders-per-PIN-node affects cache behaviour. babo_cap64_perf == default babo_perf.
if(BABO_BUILD_PIN_SWEEP)
    foreach(CAP IN ITEMS 16 32 64 128)
        add_perf(babo_cap${CAP}_perf babo_perf_order_book.cpp babobook BABO_PIN_CAPACITY=${CAP})
    endforeach()
endif()

# --- opt-in: depth OFF (lean) -----------------------------------------------
if(BABO_BUILD_NODEPTH)
    add_perf(babo_nodepth_perf   babo_perf_order_book.cpp   babobook       BABO_NO_DEPTH)
    add_perf(liqui_nodepth_perf  liqui_perf_order_book.cpp  liquibook_ref  LIQUI_NO_DEPTH)
endif()
