# --- Order-book perf executables --------------------------------------------
# Standalone binaries for hardware-counter profiling and latency checks.
# Each target links only the engine it exercises (+ spdlog for clean output).
#
# Depth ON vs OFF is a compile-time toggle, so each engine ships two binaries
# from the SAME source:
#   <engine>_perf        -> depth OFF  (lean; the fair head-to-head baseline)
#   <engine>_depth_perf  -> depth ON   (stock behaviour: maintains aggregate depth)
# babo toggles on BABO_NO_DEPTH; liquibook on LIQUI_NO_DEPTH (see
# benchmark/adapters/liqui_book_type.h, which liqui_perf shares with the adapter).

set(ADAPTERS_DIR ${CMAKE_SOURCE_DIR}/benchmark/adapters)   # for liqui_book_type.h

# helper: one perf executable = source + engine lib + spdlog, with an optional
# depth macro and (for liquibook) the shared adapter include dir.
function(add_perf target source engine_lib)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${engine_lib} spdlog::spdlog)
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
add_perf(babo_perf        babo_perf_order_book.cpp   babobook  BABO_NO_DEPTH)        # depth OFF
add_perf(babo_depth_perf  babo_perf_order_book.cpp   babobook)                       # depth ON
