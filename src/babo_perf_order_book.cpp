// Throughput benchmark for the babo matching engine -- the counterpart to
// liqui_perf_order_book.cpp, using the same workload, timing, and reporting so
// the two engines can be compared head-to-head.
//
// babo differs from liquibook in two API-level ways that this harness accounts
// for: matching_book::add() takes a SimpleOrder BY VALUE (so orders live in a
// pre-built vector, not an array of pointers), and narb_tree has no size(), so
// resting orders are counted by iteration. babo always tracks depth, so only the
// depth-5 and BBO-1 configurations exist (no "no depth" variant).
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

// Count orders still resting in the book (babo has no O(1) size()).
template <class Book>
uint32_t resting_count(Book& book) {
  uint32_t n = 0;
  for (auto it = book.bids().orders_begin(); it != book.bids().orders_end(); ++it) ++n;
  for (auto it = book.asks().orders_begin(); it != book.asks().orders_end(); ++it) ++n;
  return n;
}

// Add orders until the deadline. Returns the number added, or -1 if the pre-built
// order set was exhausted before the window elapsed.
template <class Book>
int run_test(Book& book, std::vector<SimpleOrder>& orders, clock_type::time_point end) {
  int count = 0;
  const size_t n = orders.size();
  for (size_t i = 0; i < n; ++i) {
    book.add(orders[i]);           // by value (babo copies into the book)
    ++count;
    if (clock_type::now() >= end) return count;
  }
  return -1;   // ran out of orders before time was up
}

template <class Book>
bool build_and_run_test(uint32_t dur_sec, uint32_t num_to_try) {
  std::cout << "trying run of " << num_to_try << " orders";
  Book book;
  std::vector<SimpleOrder> orders;
  orders.reserve(num_to_try);

  for (uint32_t i = 0; i < num_to_try; ++i) {
    bool is_buy((i % 2) == 0);
    uint32_t delta = is_buy ? 1880 : 1884;   // overlapping bands -> many orders cross
    uint32_t price = (rand() % 10) + delta;
    uint32_t qty = ((rand() % 10) + 1) * 100;
    orders.emplace_back(is_buy, price, qty);
  }

  clock_type::time_point start = clock_type::now();
  clock_type::time_point stop  = start + std::chrono::seconds(dur_sec);

  int count = run_test(book, orders, stop);

  if (count > 0) {
    std::cout << " - complete!" << std::endl;
    std::cout << "Inserted " << count << " orders in " << dur_sec << " seconds"
              << ", or " << count / dur_sec << " insertions per sec" << std::endl;
    uint32_t remain = resting_count(book);
    std::cout << "Run matched " << count - int(remain) << " orders" << std::endl;
    return true;
  }
  std::cout << " - not enough orders" << std::endl;
  return false;
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

  uint32_t dur_sec = 3;
  if (argc > 1) {
    dur_sec = atoi(argv[1]);
    if (!dur_sec) dur_sec = 3;
  }
  std::cout << dur_sec << " sec performance test of babo order book" << std::endl;
  srand(dur_sec);

  {
    std::cout << "testing order book with depth" << std::endl;
    uint32_t num_to_try = dur_sec * 125000;
    while (!build_and_run_test<FullDepthOrderBook>(dur_sec, num_to_try)) num_to_try *= 2;
  }
  {
    std::cout << "testing order book with bbo" << std::endl;
    uint32_t num_to_try = dur_sec * 125000;
    while (!build_and_run_test<BboOrderBook>(dur_sec, num_to_try)) num_to_try *= 2;
  }
}
