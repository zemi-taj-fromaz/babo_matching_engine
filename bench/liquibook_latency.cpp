// liquibook_ref latency benchmark (per-order add() latency distribution).
// Modeled on liquibook's src/latency_order_book.cpp, but reports p50/p90/p99/p99.9/max
// instead of dumping the raw histogram. Usage: liquibook_latency [N]  (default 1e6)
#include <simple/simple_order_book.h>

#include "bench_common.h"

int main(int argc, char** argv) {
    std::size_t n = bench::parse_n(argc, argv);
    std::cout << "liquibook_ref latency benchmark (" << n << " orders)\n";
    bench::run_latency<liquibook::simple::SimpleOrderBook<5>, liquibook::simple::SimpleOrder>(
        "liquibook_ref", bench::make_workload(n));
    return 0;
}
