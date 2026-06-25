// babo_book throughput benchmark (orders/sec through add()).
// Same harness/workload as the liquibook_ref version so the numbers are directly
// comparable. Usage: babo_throughput [N]  (default 1e6)
//
// Links babo_book only, so <simple/simple_order_book.h> resolves to babo's header.
#include <simple/simple_order_book.h>

#include "bench_common.h"

int main(int argc, char** argv) {
    std::size_t n = bench::parse_n(argc, argv);
    std::cout << "babo_book throughput benchmark (" << n << " orders)\n";
    bench::run_throughput<babo::simple::SimpleOrderBook<5>, babo::simple::SimpleOrder>(
        "babo_book", bench::make_workload(n));
    return 0;
}
