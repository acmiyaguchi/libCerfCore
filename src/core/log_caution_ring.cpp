#include "log_caution_ring.h"

#include "log_mutex.h"

#include <cerfcore/core/log.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr size_t CAUTION_RING_LINES = 12;
constexpr size_t CAUTION_LINE_MAX   = 256;
char   g_caution_ring[CAUTION_RING_LINES][CAUTION_LINE_MAX] = {};
size_t g_caution_next  = 0;
size_t g_caution_count = 0;

}

namespace log_detail {

void RecordCautionLocked(const char* fmt, va_list args) {
    char* slot = g_caution_ring[g_caution_next];
    vsnprintf(slot, CAUTION_LINE_MAX, fmt, args);
    slot[CAUTION_LINE_MAX - 1] = '\0';

    size_t len = strlen(slot);
    while (len > 0 && (slot[len - 1] == '\n' || slot[len - 1] == '\r'))
        slot[--len] = '\0';

    g_caution_next = (g_caution_next + 1) % CAUTION_RING_LINES;
    if (g_caution_count < CAUTION_RING_LINES) g_caution_count++;
}

}

void Log::CopyRecentCautionsPostFreeze(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';

    const bool owned = log_detail::LogMutex().try_lock();

    size_t written = 0;
    size_t first = (g_caution_next + CAUTION_RING_LINES - g_caution_count) % CAUTION_RING_LINES;
    for (size_t i = 0; i < g_caution_count && written + 2 < out_size; i++) {
        const char* line = g_caution_ring[(first + i) % CAUTION_RING_LINES];
        if (line[0] == '\0') continue;
        size_t k = 0;
        while (k < CAUTION_LINE_MAX && line[k] != '\0' &&
               written + 2 < out_size) {
            out[written++] = line[k++];
        }
        out[written++] = '\n';
        out[written]   = '\0';
    }

    if (owned) log_detail::LogMutex().unlock();
}
