#include "host_crash_capture.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#if !(defined(__x86_64__) || defined(__aarch64__))
#error "host_crash_capture_linux.cpp has no thread register capture for this host"
#endif

namespace cerfcore {

namespace host_platform {

namespace {

constexpr int kFreezeWaitMs = 500;

CrashThreadState* g_capture_out      = nullptr;
int               g_capture_capacity = 0;
std::atomic<int>  g_capture_count{0};

int FreezeSignal() { return SIGRTMIN + 1; }

void FreezeHandler(int, siginfo_t*, void* uc) {
    uintptr_t pc = 0, sp = 0, fp = 0;
    CrashContextRegisters(uc, pc, sp, fp);
    const int i = g_capture_count.fetch_add(1, std::memory_order_acq_rel);
    if (i < g_capture_capacity) g_capture_out[i] = CrashThreadState{CrashThreadId(), pc, sp, fp};
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
    const long self = CrashThreadId();
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

}

long CrashThreadId() { return ::syscall(SYS_gettid); }

void CrashContextRegisters(const void* uc, uintptr_t& pc, uintptr_t& sp, uintptr_t& fp) {
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

void CrashLogDirectory(char* out, size_t cap) {
    const ssize_t n = ::readlink("/proc/self/exe", out, cap - 1);
    size_t        cut = 0;
    if (n > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (out[i] == '/') cut = static_cast<size_t>(i) + 1;
        }
    }
    out[cut] = '\0';
}

int CrashCaptureOtherThreads(CrashThreadState* out, int capacity) {
    g_capture_out      = out;
    g_capture_capacity = capacity;
    g_capture_count.store(0, std::memory_order_release);
    struct sigaction sa {};
    sa.sa_sigaction = FreezeHandler;
    sa.sa_flags     = SA_SIGINFO;
    sigfillset(&sa.sa_mask);
    if (::sigaction(FreezeSignal(), &sa, nullptr) != 0) return 0;
    const int     sent     = SignalOtherThreads(FreezeSignal());
    const int64_t deadline = MonotonicMs() + kFreezeWaitMs;
    while (g_capture_count.load(std::memory_order_acquire) < sent && MonotonicMs() < deadline) {
        ::sched_yield();
    }
    const int count = g_capture_count.load(std::memory_order_acquire);
    return count < capacity ? count : capacity;
}

}

}
