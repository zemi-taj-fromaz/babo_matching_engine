# --- bench -------------------------------------------------------------------
# Throughput comparison harness. Links BOTH static libs and drives them through
# the same deterministic order stream.
add_executable(babo_bench
        bench_main.cpp
)

target_link_libraries(babo_bench
        PRIVATE
            liquibook_ref
            babo_book
)

target_compile_features(babo_bench PRIVATE cxx_std_20)

# --- standalone latency / throughput benchmarks ------------------------------
# One executable per (library, metric). Each links EXACTLY ONE book library: both
# ship include/simple/simple_order_book.h, so linking a single lib makes that
# include resolve to that library's header (no ambiguity) — the namespaces
# (liquibook:: vs babo::) keep the two harness instantiations distinct.
# They share bench_common.h (workload + measurement + reporting).
foreach(b liquibook_throughput liquibook_latency)
    add_executable(${b} ${b}.cpp)
    target_link_libraries(${b} PRIVATE liquibook_ref)
    target_compile_features(${b} PRIVATE cxx_std_20)
endforeach()

foreach(b babo_throughput babo_latency)
    add_executable(${b} ${b}.cpp)
    target_link_libraries(${b} PRIVATE babo_book)
    target_compile_features(${b} PRIVATE cxx_std_20)
endforeach()
