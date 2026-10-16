#include <cerfcore/core/log.h>
#include "../platform/host_crash.h"
#include <cerfcore/platform/host_process.h>
#include "../platform/host_thread.h"
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>

namespace cerfcore {

static std::atomic<bool> g_emergency_started{false};
static std::atomic<Log::CrashPresenter> g_crash_presenter{nullptr};

void Log::SetCrashPresenter(CrashPresenter presenter) {
    g_crash_presenter.store(presenter, std::memory_order_release);
}

void Log::EmergencyDumpAllThreadStacks() {
    static std::atomic<bool> dumped{false};
    bool expected = false;
    if (!dumped.compare_exchange_strong(expected, true)) return;
    host_platform::CrashDumpOtherThreads();
}

bool Log::IsEmergencyActive() {
    return g_emergency_started.load(std::memory_order_acquire);
}

void Log::EmergencyStart() {
    bool expected = false;
    if (!g_emergency_started.compare_exchange_strong(expected, true))
        return;
    host_platform::CrashFreezeOtherThreads();
    host_platform::CrashOpenLog();
}

void Log::Emergency(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = host_platform::CrashFormat(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    host_platform::CrashWrite(buf, (size_t)n);
}

void Log::EmergencyWriteBytes(const char* buf, size_t len) {
    host_platform::CrashWrite(buf, len);
}

void Log::EmergencyPrintNativeStack(const char* tag) {
    host_platform::CrashPrintNativeStack(tag);
}

bool Log::SymbolizeAddress(const void* addr, char* out, size_t out_size) {
    return host_platform::CrashSymbolize(addr, out, out_size);
}

void Log::InstallCrashHandler() {
    host_platform::CrashInstallUnhandledFilter();
}

void CerfFatalExit(int code) {
    char recent_cautions[4096];
    recent_cautions[0] = '\0';

    if (code == CERF_FATAL_NORMAL_EXIT || code == CERF_FATAL_TIMEOUT)
        LOG(Cerf, "CERF is exiting (code = %d)\n", code);
    else
        LOG(Caution, "CERF is halting (fatal code = %d)\n", code);

    if (code == CERF_FATAL_USER_ERROR || code == CERF_FATAL_NORMAL_EXIT ||
        code == CERF_FATAL_TIMEOUT) {
        Log::Close();
        host_platform::ExitProcessNow(code);
    }

    LOG(Cerf, "this run ends here; the crash tail is in cerf.crash.log\n");
    Log::Close();

    Log::EmergencyStart();
    Log::CopyRecentCautionsPostFreeze(recent_cautions,
                                      sizeof(recent_cautions));
    if (recent_cautions[0]) {
        Log::Emergency("[FATAL] recent cautions:\n");
        host_platform::CrashWrite(recent_cautions, strlen(recent_cautions));
    }
    Log::EmergencyDumpAllThreadStacks();
    Log::Emergency("[FATAL] CerfFatalExit(%d) tid=%lu stack trace:\n",
                   code, (unsigned long)host_platform::CurrentThreadId());
    Log::EmergencyPrintNativeStack("[FATAL]");
    Log::Close();

#if !CERF_DEV_MODE
    if (code == CERF_FATAL_RUNTIME_ERROR) {
        if (const Log::CrashPresenter present = g_crash_presenter.load(std::memory_order_acquire))
            present(Log::FilePath());
    }
#endif

    host_platform::ExitProcessNow(code);
}

}
