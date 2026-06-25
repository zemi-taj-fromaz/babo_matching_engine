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
