#include <cerfcore/core/fatal.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfengine/guest_engine.h>

#include <cstdarg>
#include <cstdio>

REGISTER_SERVICE(Fatal);

void Fatal::Die(const char* fmt, ...) {
    char reason[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(reason, sizeof(reason), fmt, ap);
    va_end(ap);

    if (cerfengine::GuestEngine* engine = live_engine_.load(std::memory_order_acquire))
        engine->PrintFatalDump();
    else
        LOG(Caution, "no guest context: the engine is not live\n");

    LOG(Caution, "%s\n", reason);

    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}
