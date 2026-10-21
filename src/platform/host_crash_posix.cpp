#include "host_crash.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <cerfcore/core/log.h>

#if !defined(__linux__) || !(defined(__x86_64__) || defined(__aarch64__))
#error "host_crash_posix.cpp has no thread register capture for this host"
#endif

namespace cerfcore {

namespace host_platform {

namespace {

constexpr int kMaxFrozenThreads = 128;
constexpr int kStackSlots       = 16;
constexpr int kFreezeWaitMs     = 500;

struct FrozenThread {
    long      tid;
    uintptr_t pc;
    uintptr_t sp;
    uintptr_t fp;
};

FrozenThread     g_frozen[kMaxFrozenThreads];
std::atomic<int> g_frozen_count{0};
int              g_crash_fd = -1;

long ThisThreadId() { return ::syscall(SYS_gettid); }

int FreezeSignal() { return SIGRTMIN + 1; }

void ContextRegisters(const void* uc, uintptr_t& pc, uintptr_t& sp, uintptr_t& fp) {
    const mcontext_t& m = static_cast<const ucontext_t*>(uc)->uc_mcontext;
#if defined(__x86_64__)
    pc = static_cast<uintptr_t>(m.gregs[REG_RIP]);
    sp = static_cast<uintptr_t>(m.gregs[REG_RSP]);
    fp = static_cast<uintptr_t>(m.gregs[REG_RBP]);
#else
    pc = static_cast<uintptr_t>(m.pc);
    sp = static_cast<uintptr_t>(m.sp);
    fp = static_cast<uintptr_t>(m.regs[29]);
#endif
}

void FreezeHandler(int, siginfo_t*, void* uc) {
    const int i = g_frozen_count.fetch_add(1, std::memory_order_acq_rel);
    if (i < kMaxFrozenThreads) {
        FrozenThread& t = g_frozen[i];
        t.tid = ThisThreadId();
        ContextRegisters(uc, t.pc, t.sp, t.fp);
    }
    sigset_t none;
    sigemptyset(&none);
    for (;;) sigsuspend(&none);
}

int64_t MonotonicMs() {
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

int SignalOtherThreads(int sig) {
    const int dir = ::open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return 0;
    const long self = ThisThreadId();
    const long pid  = static_cast<long>(::getpid());
    int        sent = 0;
    alignas(8) char buf[4096];
    for (;;) {
        const long n = ::syscall(SYS_getdents64, dir, buf, sizeof(buf));
        if (n <= 0) break;
        for (long off = 0; off < n;) {
            const char*          rec    = buf + off;
            const unsigned short reclen = *reinterpret_cast<const unsigned short*>(rec + 16);
            const char*          name   = rec + 19;
            off += reclen;
            if (name[0] < '0' || name[0] > '9') continue;
            const long tid = std::strtol(name, nullptr, 10);
            if (tid == self) continue;
            if (::syscall(SYS_tgkill, pid, tid, sig) == 0) ++sent;
        }
    }
    ::close(dir);
    return sent;
}

void ExecutableDirectory(char* out, size_t cap) {
    const ssize_t n = ::readlink("/proc/self/exe", out, cap - 1);
    size_t        cut = 0;
    if (n > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (out[i] == '/') cut = static_cast<size_t>(i) + 1;
        }
    }
    out[cut] = '\0';
}

void UnhandledSignal(int sig, siginfo_t* info, void* uc) {
    LOG(Cerf, "this run ends here; the crash tail is in cerf.crash.log\n");
    Log::Close();
    Log::EmergencyStart();
    char recent_cautions[4096];
    Log::CopyRecentCautionsPostFreeze(recent_cautions, sizeof(recent_cautions));
    if (recent_cautions[0]) {
        Log::Emergency("[UNHANDLED] recent cautions:\n");
        CrashWrite(recent_cautions, std::strlen(recent_cautions));
    }
    uintptr_t pc = 0, sp = 0, fp = 0;
    ContextRegisters(uc, pc, sp, fp);
    Log::Emergency("[UNHANDLED] signal=%d (%s) code=%d\n", sig, strsignal(sig),
                   info != nullptr ? info->si_code : 0);
    if (info != nullptr && (sig == SIGSEGV || sig == SIGBUS)) {
        Log::Emergency("[UNHANDLED] access violation at address %p\n", info->si_addr);
    }
    char sym[512];
    if (CrashSymbolize(reinterpret_cast<const void*>(pc), sym, sizeof(sym))) {
        Log::Emergency("[UNHANDLED] fault at %s (%p)\n", sym, reinterpret_cast<void*>(pc));
    }
    Log::Emergency("[UNHANDLED] PC=%p SP=%p FP=%p tid=%ld\n", reinterpret_cast<void*>(pc),
                   reinterpret_cast<void*>(sp), reinterpret_cast<void*>(fp), ThisThreadId());
    Log::EmergencyDumpAllThreadStacks();
    CrashPrintNativeStack("[UNHANDLED]");
    Log::Close();
    ::_exit(CERF_FATAL_RUNTIME_ERROR);
}

}

void CrashWrite(const char* buf, size_t len) {
    if (!buf || len == 0) return;
    if (g_crash_fd >= 0) {
        if (::write(g_crash_fd, buf, len) < 0) {}
    }
    if (::write(STDERR_FILENO, buf, len) < 0) {}
}

int CrashFormat(char* buf, size_t cap, const char* fmt, va_list ap) {
    const int n = std::vsnprintf(buf, cap, fmt, ap);
    if (n < 0) return n;
    return static_cast<size_t>(n) < cap ? n : static_cast<int>(cap - 1);
}

void CrashFreezeOtherThreads() {
    struct sigaction sa {};
    sa.sa_sigaction = FreezeHandler;
    sa.sa_flags     = SA_SIGINFO;
    sigfillset(&sa.sa_mask);
    if (::sigaction(FreezeSignal(), &sa, nullptr) != 0) return;
    const int     sent     = SignalOtherThreads(FreezeSignal());
    const int64_t deadline = MonotonicMs() + kFreezeWaitMs;
    while (g_frozen_count.load(std::memory_order_acquire) < sent && MonotonicMs() < deadline) {
        ::sched_yield();
    }
}

void CrashOpenLog() {
    char path[4096];
    ExecutableDirectory(path, sizeof(path) - 16);
    std::strcat(path, "cerf.crash.log");
    g_crash_fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

    timespec now{};
    ::clock_gettime(CLOCK_REALTIME, &now);
    const time_t secs = now.tv_sec;
    std::tm      local{};
    ::localtime_r(&secs, &local);
    char hdr[256];
    int  n = std::snprintf(hdr, sizeof(hdr),
                           "=== CERF CRASH %04d-%02d-%02d %02d:%02d:%02d.%03ld (tid=%ld pid=%ld) ===\n",
                           local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                           local.tm_min, local.tm_sec, now.tv_nsec / 1000000, ThisThreadId(),
                           static_cast<long>(::getpid()));
    if (n > 0) CrashWrite(hdr, static_cast<size_t>(n));
    Dl_info self{};
    if (::dladdr(reinterpret_cast<const void*>(&CrashOpenLog), &self) != 0) {
        n = std::snprintf(hdr, sizeof(hdr),
                          "=== executable runtime base=%p (offset = addr - base) ===\n",
                          self.dli_fbase);
        if (n > 0) CrashWrite(hdr, static_cast<size_t>(n));
    }
}

void CrashDumpOtherThreads() {
    const int count = g_frozen_count.load(std::memory_order_acquire);
    Log::Emergency("\n=== All other threads' state at crash (frozen) ===\n");
    for (int i = 0; i < count && i < kMaxFrozenThreads; ++i) {
        const FrozenThread& t = g_frozen[i];
        Log::Emergency("--- tid=%ld  PC=%p  SP=%p  FP=%p\n", t.tid,
                       reinterpret_cast<void*>(t.pc), reinterpret_cast<void*>(t.sp),
                       reinterpret_cast<void*>(t.fp));
        if (t.sp == 0) continue;
        const uintptr_t* slot = reinterpret_cast<const uintptr_t*>(t.sp);
        for (int s = 0; s < kStackSlots; ++s) {
            Log::Emergency("     [sp+%02X] 0x%016llX\n", s * static_cast<int>(sizeof(uintptr_t)),
                           static_cast<unsigned long long>(slot[s]));
        }
    }
    Log::Emergency("=== end frozen thread dump ===\n\n");
}

void CrashPrintNativeStack(const char* tag) {
    void*     frames[32];
    const int n = ::backtrace(frames, 32);
    char      line[640];
    char      sym[512];
    for (int i = 0; i < n; ++i) {
        const int ln = CrashSymbolize(frames[i], sym, sizeof(sym))
                           ? std::snprintf(line, sizeof(line), "%s   [%d] %s\n", tag, i, sym)
                           : std::snprintf(line, sizeof(line), "%s   [%d] %p\n", tag, i, frames[i]);
        if (ln > 0) CrashWrite(line, static_cast<size_t>(ln) < sizeof(line) ? static_cast<size_t>(ln)
                                                                                : sizeof(line) - 1);
    }
}

bool CrashSymbolize(const void* addr, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    Dl_info info{};
    if (::dladdr(addr, &info) == 0) return false;
    const uintptr_t at = reinterpret_cast<uintptr_t>(addr);
    if (info.dli_sname != nullptr && info.dli_saddr != nullptr) {
        int   status    = 0;
        char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
        std::snprintf(out, out_size, "%s+0x%lX", status == 0 ? demangled : info.dli_sname,
                      static_cast<unsigned long>(at - reinterpret_cast<uintptr_t>(info.dli_saddr)));
        std::free(demangled);
        return true;
    }
    if (info.dli_fname != nullptr) {
        const char* base = std::strrchr(info.dli_fname, '/');
        std::snprintf(out, out_size, "%s+0x%lX", base != nullptr ? base + 1 : info.dli_fname,
                      static_cast<unsigned long>(at - reinterpret_cast<uintptr_t>(info.dli_fbase)));
        return true;
    }
    return false;
}

void CrashInstallUnhandledFilter() {
    void* warm[1];
    ::backtrace(warm, 1);
    struct sigaction sa {};
    sa.sa_sigaction = UnhandledSignal;
    sa.sa_flags     = SA_SIGINFO | SA_RESETHAND;
    sigfillset(&sa.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) ::sigaction(sig, &sa, nullptr);
}

}

}
