# --- Order-book perf executables --------------------------------------------
# Standalone binaries for hardware-counter profiling and latency checks.
# Each target links only the engine it exercises.

add_executable(liqui_perf liqui_perf_order_book.cpp)
target_link_libraries(liqui_perf PRIVATE liquibook_ref)
target_compile_features(liqui_perf PRIVATE cxx_std_20)


add_executable(babo_perf babo_perf_order_book.cpp)
target_link_libraries(babo_perf PRIVATE babobook)
target_compile_features(babo_perf PRIVATE cxx_std_20)
