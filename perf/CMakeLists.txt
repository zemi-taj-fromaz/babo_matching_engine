# --- Order-book perf executables --------------------------------------------
# Standalone binaries for hardware-counter profiling and latency checks.
# Each target consumes only the engine target it exercises. Console formatting is a
# dependency-free header (perf/bench_log.h, ANSI colors).
#
# Pull-based depth is part of the canonical build; there is no separate no-depth
# benchmark matrix because the optimized implementation measured no meaningful
# difference. The only opt-in perf variants are the non-default PIN capacities:
#   -DBABO_BUILD_PIN_SWEEP=ON  -> babo_cap{16,32,128}_perf

set(ADAPTERS_DIR ${CMAKE_SOURCE_DIR}/benchmark/adapters)   # for liqui_book_type.h

# helper: one perf executable = source + engine target, with optional compile-time
# experiment definitions and the shared adapter include dir.
function(add_perf target source engine_target)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${engine_target})
    target_include_directories(${target} PRIVATE ${ADAPTERS_DIR})
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(ARGN)
        target_compile_definitions(${target} PRIVATE ${ARGN})
    endif()
endfunction()

# --- canonical targets built by default ------------------------------------
add_perf(liqui_perf  liqui_perf_order_book.cpp  liquibook_ref)
add_perf(babo_perf   babo_perf_order_book.cpp   babobook::babobook)        # PIN capacity 64

# Cancel-throughput-vs-resting-book-size sweep (the O(1)-cancel "money figure").
# Same engine targets, a synthetic prefill+cancel workload; see scaling_common.h.
add_perf(liqui_scaling  liqui_scaling.cpp  liquibook_ref)
add_perf(babo_scaling   babo_scaling.cpp   babobook::babobook)

# --- opt-in: pin_node capacity sweep (depth ON) -----------------------------
# One target per non-default capacity. `babo_perf` already represents capacity
# 64, so the opt-in sweep does not compile a duplicate cap64 binary.
if(BABO_BUILD_PIN_SWEEP)
    foreach(CAP IN ITEMS 16 32 128)
        add_perf(babo_cap${CAP}_perf babo_perf_order_book.cpp babobook::babobook BABO_PIN_CAPACITY=${CAP})
    endforeach()
endif()
