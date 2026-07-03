# --- Order-book benchmark executables ---------------------------------------
# Latency (per-order ns) and throughput (inserts/sec) harnesses for each engine.
# Standalone (no PCH/spdlog); each links only the engine it exercises.
# (Sources are relative to this dir; requires liquibook_ref / babobook, so the
# parent adds this subdirectory after those libraries.)
add_executable(liqui_perf    liqui_perf_order_book.cpp)
add_executable(liqui_latency liqui_latency_order_book.cpp)
target_link_libraries(liqui_perf    PRIVATE liquibook_ref)
target_link_libraries(liqui_latency PRIVATE liquibook_ref)

add_executable(babo_perf     babo_perf_order_book.cpp)
add_executable(babo_latency  babo_latency_order_book.cpp)
target_link_libraries(babo_perf    PRIVATE babobook)
target_link_libraries(babo_latency PRIVATE babobook)
