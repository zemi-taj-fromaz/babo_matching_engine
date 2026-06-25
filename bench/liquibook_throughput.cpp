// liquibook_ref throughput benchmark (orders/sec through add()).
// Modeled on liquibook's src/perf_order_book.cpp. Usage: liquibook_throughput [N]  (default 1e6)
//
// Links liquibook_ref only, so <simple/simple_order_book.h> resolves to its header
// (babo_book ships the same path; linking one library avoids the collision).
#include <simple/simple_order_book.h>

#include "bench_common.h"

int main(int argc, char** argv) {
    std::size_t n = bench::parse_n(argc, argv);
    std::cout << "liquibook_ref throughput benchmark (" << n << " orders)\n";
    bench::run_throughput<liquibook::simple::SimpleOrderBook<5>, liquibook::simple::SimpleOrder>(
        "liquibook_ref", bench::make_workload(n));
    return 0;
}
