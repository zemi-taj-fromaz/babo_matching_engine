// platform.hpp — cross-platform shims for the harness's OS-specific bits:
// dynamic library loading, thread affinity, process id, plugin suffix, and a
// portable watchdog that replaces alarm()/SIGALRM. Windows (Win32) + Linux
// (pthread/dlfcn) + macOS (dlfcn; no hard pinning).
#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#  if defined(__linux__)
#    include <sched.h>
#    include <pthread.h>
#  endif
#endif

namespace plat {

// ---- dynamic library loading --------------------------------------------
// Handles are kept as void* so the harness's EngineLib.handle stays type-stable
// across platforms (HMODULE is a pointer, so the round-trip cast is safe).
inline void* dl_open(const char* path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(path));   // RTLD_NOW|RTLD_LOCAL-like
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

inline void* dl_sym(void* handle, const char* sym) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), sym));
#else
    return dlsym(handle, sym);
#endif
}

inline void dl_close(void* handle) {
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

inline std::string dl_error() {
#if defined(_WIN32)
    DWORD e = GetLastError();
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, buf, sizeof(buf), nullptr);
    return buf;
#else
    const char* e = dlerror();
    return e ? e : "";
#endif
}

// Plugin filename suffix: babobook_adapter.dll vs babobook_adapter.so
inline const char* engine_suffix() {
#if defined(_WIN32)
    return "_adapter.dll";
#else
    return "_adapter.so";
#endif
}

// ---- thread affinity (pins the CALLING thread) --------------------------
// core < 0 => opt out (returns true). Returns false if pinning is unsupported
// or fails — the harness gates such a run INVALID, which is correct: an
// unpinned run's throughput is meaningless. macOS has no hard pinning, so it
// returns false there by design.
inline bool pin_current_thread(int core) {
    if (core < 0) return true;
#if defined(_WIN32)
    return SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << core) != 0;
#elif defined(__linux__)
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    return false;   // e.g. macOS: no real per-core pinning
#endif
}

// ---- process id (used only to salt the audit seed) ----------------------
inline unsigned long process_id() {
#if defined(_WIN32)
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

// ---- portable watchdog (replaces alarm()/SIGALRM) -----------------------
// Spawns a thread that fires `on_timeout` after `seconds` unless cancel()/the
// destructor runs first. The callback runs in normal thread context (not a
// signal handler), so it may do ordinary I/O before terminating the process.
class Watchdog {
public:
    Watchdog(unsigned seconds, std::function<void()> on_timeout) {
        if (seconds == 0) return;
        thread_ = std::thread([this, seconds, cb = std::move(on_timeout)] {
            std::unique_lock<std::mutex> lk(m_);
            if (!cv_.wait_for(lk, std::chrono::seconds(seconds),
                              [this] { return cancelled_; }))
                cb();   // timed out before cancellation
        });
    }
    void cancel() {
        { std::lock_guard<std::mutex> lk(m_); cancelled_ = true; }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    ~Watchdog() { cancel(); }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    std::thread             thread_;
    std::mutex              m_;
    std::condition_variable cv_;
    bool                    cancelled_ = false;
};

} // namespace plat
