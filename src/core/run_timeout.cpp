#include <cerfcore/core/run_timeout.h>
#include <cerfcore/core/log.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {
std::atomic<int> g_run_timeout_seconds{0};
}

void RunTimeout::Start(int seconds) {
    if (seconds <= 0) return;
    g_run_timeout_seconds.store(seconds, std::memory_order_release);
    LOG(Cerf, "run timeout set: %d s\n", seconds);
    std::thread([seconds] {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        LOG(Cerf, "run timeout expired after %d s\n", seconds);
        CerfFatalExit(CERF_FATAL_TIMEOUT);
    }).detach();
}

bool RunTimeout::IsActive() {
    return g_run_timeout_seconds.load(std::memory_order_acquire) > 0;
}
