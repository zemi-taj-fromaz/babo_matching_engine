//
// Created by hrcol on 5.7.2026..
//
/*
 * audit.cpp — anti-cheat checks.
 *
 *  - current_thread_count(): reports how many threads the process holds. The
 *    harness samples it after engine_init() and after the run and records both
 *    in the result JSON. An engine is free to use threads — this number is
 *    informational only and never gates the verdict.
 *
 *  - run_state_audit(): the random-point order-book state audit. A hardcoded or
 *    replay-only engine can reproduce the published report-stream hash but
 *    cannot answer live best-bid / best-ask / depth queries about a book it
 *    never maintained. The audit replays the workload through a trusted public
 *    baseline engine and compares its query answers, at unpredictable indices,
 *    to the answers the engine under test gave during its own run. The ground
 *    truth is a public engine — no proprietary code is involved.
 */
#include "harness.h"

// Per-OS thread enumeration: Windows Toolhelp, macOS Mach, Linux procfs.
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX               // keep <windows.h> from clobbering std::min below
#  include <windows.h>
#  include <tlhelp32.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#elif defined(__linux__)
#  include <dirent.h>
#endif

#include <algorithm>
#include <random>
#include <set>
#include <vector>

// Threads in THIS process. Informational only (never gates the verdict), so it
// returns -1 where unsupported. Each OS exposes thread enumeration differently.
int current_thread_count() {
#if defined(_WIN32)
    // Toolhelp snapshots ALL system threads; filter to our own process id.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);                          // required before Thread32First
    const DWORD me = GetCurrentProcessId();
    int n = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == me) ++n;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return n;
#elif defined(__APPLE__)
    thread_act_array_t threads;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) return -1;
    vm_deallocate(mach_task_self(), (vm_address_t)threads,
                  count * sizeof(thread_act_t));      // free the array Mach allocated
    return static_cast<int>(count);
#elif defined(__linux__)
    // Each thread of the process appears as a directory under /proc/self/task.
    DIR* d = opendir("/proc/self/task");
    if (!d) return -1;
    int n = 0;
    for (struct dirent* e; (e = readdir(d)) != nullptr; )
        if (e->d_name[0] != '.') ++n;
    closedir(d);
    return n;
#else
    return -1;   // unsupported platform: report "unknown"
#endif
}

namespace {

/* Apply one workload message to `e` for its order-book state. The engine-call
 * sequence mirrors the dispatch loop in harness.cpp so this replay reconstructs
 * the identical order book. The reports the baseline emits are discarded by the
 * null transport (see run_state_audit) — only book state is reproduced here. */
void feed(EngineLib& e, const WorkloadMsg& m) {
    if (m.type == 0) {                               /* NEW */
        new_order_t no{};
        no.order_id = m.order_id;       no.sequence_number = m.seq;
        no.price_ticks = m.price_ticks; no.quantity = m.qty;
        no.side = m.side;               no.ioc = m.ioc;
        e.on_new_order(&no);
    } else if (m.type == 1) {                        /* CANCEL */
        cancel_t c{};
        c.order_id = m.order_id; c.sequence_number = m.seq;
        e.on_cancel(&c);
    } else {                                         /* MODIFY */
        modify_t md{};
        md.order_id = m.order_id;             md.sequence_number = m.seq;
        md.new_price_ticks = m.price_ticks;   md.new_quantity = m.qty;
        md.side = m.side;
        e.on_modify(&md);
    }
}

/* Mirror of feed() for the optional engine_prebuild hook: build each message's
 * ABI struct and hand it to the baseline's pre-build, so a baseline that
 * pre-converts has its native orders ready before the replay calls engine_on_*. */
void prebuild_msg(EngineLib& e, const WorkloadMsg& m) {
    if (m.type == 0) {
        new_order_t no{};
        no.order_id = m.order_id;       no.sequence_number = m.seq;
        no.price_ticks = m.price_ticks; no.quantity = m.qty;
        no.side = m.side;               no.ioc = m.ioc;
        e.prebuild(0, &no);
    } else if (m.type == 1) {
        cancel_t c{};
        c.order_id = m.order_id; c.sequence_number = m.seq;
        e.prebuild(1, &c);
    } else {
        modify_t md{};
        md.order_id = m.order_id;           md.sequence_number = m.seq;
        md.new_price_ticks = m.price_ticks; md.new_quantity = m.qty;
        md.side = m.side;
        e.prebuild(2, &md);
    }
}

}  // namespace

std::vector<size_t> audit_pick_indices(uint64_t verify_seed, size_t count, int n) {
    std::vector<size_t> out;
    if (count == 0 || n <= 0) return out;
    std::mt19937_64 rng(verify_seed);
    std::set<size_t> picked;                         /* std::set iterates sorted */
    size_t want = std::min<size_t>(static_cast<size_t>(n), count);
    while (picked.size() < want) picked.insert(rng() % count);
    out.assign(picked.begin(), picked.end());
    return out;
}

AuditReport run_state_audit(EngineLib& baseline,
                            const std::vector<WorkloadMsg>& workload,
                            const std::vector<QuerySnapshot>& under_test) {
    AuditReport r;
    r.ran = true;
    if (under_test.empty()) {
        r.note = "no state snapshots were recorded";
        return r;
    }

    /* The baseline's report stream is not needed — only its book state — so it
     * is initialised with the discard transport. */
    const me_transport_t* nt = harness_null_transport();
    void* sink = nt->create(0);
    baseline.init(0, nt, sink);                      /* seed unused by a replay */

    /* Honour the engine_prebuild contract: if the baseline pre-converts, drive
     * its pre-build before the replay — the replay's engine_on_* may consume the
     * pre-built form. Mirrors the dispatch loop in harness.cpp. */
    if (baseline.prebuild)
        for (const WorkloadMsg& m : workload) prebuild_msg(baseline, m);

    size_t cur = 0;
    for (size_t i = 0; i < workload.size() && cur < under_test.size(); ++i) {
        feed(baseline, workload[i]);
        if (under_test[cur].index != i) continue;

        const QuerySnapshot& ut = under_test[cur];
        int64_t  bb = baseline.query_best_bid();
        int64_t  ba = baseline.query_best_ask();
        uint64_t dp = baseline.query_depth_at(ut.probe_price, ut.probe_side);
        r.checks += 3;
        if (bb != ut.best_bid) ++r.mismatches;
        if (ba != ut.best_ask) ++r.mismatches;
        if (dp != ut.depth)    ++r.mismatches;
        ++cur;
    }
    baseline.flush();
    baseline.shutdown();
    nt->destroy(sink);

    r.passed = (r.checks > 0 && r.mismatches == 0);
    if (r.passed)
        r.note = "order-book state matched the baseline at every probe";
    else
        r.note = "engine_query_* answers diverged from the baseline";
    return r;
}
