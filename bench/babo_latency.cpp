// babo_book latency benchmark (per-order add() latency distribution).
// Same harness/workload as the liquibook_ref version. Reports p50/p90/p99/p99.9/max.
// Usage: babo_latency [N]  (default 1e6)
#include <simple/simple_order_book.h>

#include "bench_common.h"

int main(int argc, char** argv) {
    std::size_t n = bench::parse_n(argc, argv);
    std::cout << "babo_book latency benchmark (" << n << " orders)\n";
    bench::run_latency<babo::simple::SimpleOrderBook<5>, babo::simple::SimpleOrder>(
        "babo_book", bench::make_workload(n));
    return 0;
}
