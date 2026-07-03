// Per-order latency benchmark for the babo matching engine -- the counterpart to
// liqui_latency_order_book.cpp, using the same workload, timer, and output format.
//
// It timestamps immediately before each matching_book::add() and prints the
// elapsed nanoseconds per order, one value per line. A warm-up "dry run" primes
// caches/predictor/allocator before the measured runs. babo's add() takes the
// order by value, and it always tracks depth (depth-5 and BBO-1 variants only).
// --- CPU core isolation -----------------------------------------------------
// Pin this thread to a single core so its per-core L1/L2 caches stay warm for
// the whole run (the OS never migrates it to a cold core). Windows + Linux.
// Affinity only; keeping *other* work off the core additionally needs OS setup
// (Linux boot: isolcpus=<n> nohz_full=<n>, or raising thread priority).
#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE            // sched_setaffinity / CPU_SET live behind this
#endif
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX               // keep <windows.h> from clobbering std::min/max
#  include <windows.h>
static bool pin_to_core(unsigned core) {
  return SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << core) != 0;
}
// Windows has no isolcpus equivalent, so raising priority IS the programmatic
// "isolation": this thread preempts other normal work the scheduler may still
// place on the pinned core. TIME_CRITICAL is aggressive but recoverable;
// REALTIME_PRIORITY_CLASS (commented) needs admin and can wedge the machine if
// the thread ever busy-waits.
static bool isolate_core() {
  // SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
  return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
}
#else
#  include <sched.h>
static bool pin_to_core(unsigned core) {
  cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;   // 0 == calling thread
}
// Programmatic isolation on Linux is real-time scheduling: SCHED_FIFO preempts
// all normal (SCHED_OTHER) tasks on the core. Needs CAP_SYS_NICE/root; returns
// false otherwise. NOTE: isolcpus / nohz_full are BOOT parameters -- they cannot
// be set from within the process; this is the runtime approximation of them.
static bool isolate_core() {
  struct sched_param sp;
  sp.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
  return sched_setscheduler(0, SCHED_FIFO, &sp) == 0;
}
#endif
static constexpr unsigned kBenchCore = 2;   // change to a free physical core

#include <book/matching_book.h>
#include <simple/simple_order.h>

#include <chrono>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <vector>

using babo::simple::SimpleOrder;

using clock_type = std::chrono::steady_clock;

typedef babo::book::matching_book<5> FullDepthOrderBook;
typedef babo::book::matching_book<1> BboOrderBook;

// Print elapsed ns between consecutive timestamps (one add() per gap).
void build_histogram(const std::vector<clock_type::time_point>& ts) {
  std::cout << "Latency (ns) " << std::endl;
  for (size_t i = 1; i < ts.size(); ++i) {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ts[i] - ts[i - 1]).count();
    std::cout << ns << std::endl;
  }
}

// Stamp the clock before each add; a final stamp closes the last interval.
template <class Book>
void run_test(Book& book, std::vector<SimpleOrder>& orders,
              std::vector<clock_type::time_point>& ts) {
  const size_t n = orders.size();
  for (size_t i = 0; i < n; ++i) {
    ts[i] = clock_type::now();
    book.add(orders[i]);
  }
  ts[n] = clock_type::now();   // close the final interval
}

template <class Book>
void build_and_run_test(uint32_t num_to_try, bool dry_run = false) {
  Book book;
  std::vector<SimpleOrder> orders;
  orders.reserve(num_to_try);
  std::vector<clock_type::time_point> ts(num_to_try + 1);

  for (uint32_t i = 0; i < num_to_try; ++i) {
    bool is_buy((i % 2) == 0);
    uint32_t delta = is_buy ? 1880 : 1884;
    uint32_t price = (rand() % 10) + delta;
    uint32_t qty = ((rand() % 10) + 1) * 100;
    orders.emplace_back(is_buy, price, qty);
  }

  run_test(book, orders, ts);
  std::cout << " - complete!" << std::endl;

  if (!dry_run) {
    std::cout << "Building histogram" << std::endl;
    build_histogram(ts);
  }
}

int main(int argc, const char* argv[]) {
  if (pin_to_core(kBenchCore))
    std::cerr << "pinned to CPU core " << kBenchCore << std::endl;
  else
    std::cerr << "warning: could not pin to CPU core " << kBenchCore << std::endl;
  if (isolate_core())
    std::cerr << "raised to real-time priority (isolating the core)" << std::endl;
  else
    std::cerr << "warning: could not raise priority (need admin/root?)" << std::endl;

  uint32_t num_to_try = 10000;
  if (argc > 1) {
    num_to_try = atoi(argv[1]);
    if (!num_to_try) num_to_try = 10000;
  }
  std::cout << num_to_try << " order latency test of babo order book" << std::endl;
  srand(num_to_try);

  std::cout << "starting dry run" << std::endl;
  build_and_run_test<FullDepthOrderBook>(num_to_try, /*dry_run=*/true);

  std::cout << "testing order book with depth" << std::endl;
  build_and_run_test<FullDepthOrderBook>(num_to_try);

  std::cout << "testing order book with bbo" << std::endl;
  build_and_run_test<BboOrderBook>(num_to_try);
}
