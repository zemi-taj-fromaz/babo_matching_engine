// Portable single-core pinning + priority isolation for the comparison harness.
// Windows (SetThreadAffinityMask + TIME_CRITICAL), Linux (sched_setaffinity +
// SCHED_FIFO), no-op elsewhere (e.g. macOS, which has no real thread pinning).
#pragma once

#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif

#include <cstdio>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
inline bool pin_to_core(unsigned core) {
  return SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << core) != 0;
}
inline bool isolate_core() {
  return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
}
#elif defined(__linux__)
#  include <sched.h>
inline bool pin_to_core(unsigned core) {
  cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
}
inline bool isolate_core() {
  struct sched_param sp; sp.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
  return sched_setscheduler(0, SCHED_FIFO, &sp) == 0;
}
#else
inline bool pin_to_core(unsigned)  { return false; }  // e.g. macOS: no hard pinning
inline bool isolate_core()         { return false; }
#endif

// Pin + isolate the calling thread; report to stderr (keeps stdout data clean).
inline void pin_and_isolate(unsigned core) {
  std::fprintf(stderr, pin_to_core(core) ? "[pin] core %u\n"
                                         : "[pin] could not pin to core %u\n", core);
  std::fprintf(stderr, isolate_core() ? "[pin] real-time priority\n"
                                      : "[pin] no priority bump (need admin/root?)\n");
}
