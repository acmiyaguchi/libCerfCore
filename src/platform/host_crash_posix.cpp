#include "host_crash.h"

#include "host_crash_capture.h"

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
#include <unistd.h>

#include <cerfcore/core/log.h>

namespace cerfcore {

namespace host_platform {

namespace {

constexpr int kMaxFrozenThreads = 128;
constexpr int kStackSlots       = 16;

CrashThreadState g_frozen[kMaxFrozenThreads];
std::atomic<int> g_frozen_count{0};
int              g_crash_fd = -1;

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
    CrashContextRegisters(uc, pc, sp, fp);
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
                   reinterpret_cast<void*>(sp), reinterpret_cast<void*>(fp), CrashThreadId());
    Log::EmergencyDumpAllThreadStacks();
    CrashPrintNativeStack("[UNHANDLED]");
    Log::Close();
    ::_exit(CERF_FATAL_RUNTIME_ERROR);
}

}

void CrashFreezeOtherThreads() {
    g_frozen_count.store(CrashCaptureOtherThreads(g_frozen, kMaxFrozenThreads),
                         std::memory_order_release);
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

void CrashOpenLog() {
    char path[4096];
    CrashLogDirectory(path, sizeof(path) - 16);
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
                           local.tm_min, local.tm_sec, now.tv_nsec / 1000000, CrashThreadId(),
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
        const CrashThreadState& t = g_frozen[i];
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
