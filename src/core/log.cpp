#include <cerfcore/core/log.h>
#include <cerfcore/core/cerf_paths.h>
#include "log_caution_ring.h"
#include "log_mutex.h"
#include "steady_time.h"
#include "../platform/host_clock.h"
#include "../platform/host_error.h"
#include "../platform/host_event.h"
#include <cerfcore/platform/host_file.h>
#include <cerfcore/platform/host_process.h>
#include "../platform/host_thread.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <system_error>
#include <thread>

namespace cerfcore {

#if CERF_DEV_MODE
std::atomic<uint64_t> Log::detail::enabled_mask{Log::MASK_ALL};
#else
std::atomic<uint64_t> Log::detail::enabled_mask{Log::MASK_PRODUCTION_DEFAULT};
#endif
static std::atomic<host_platform::HostFile*> g_log_file{nullptr};
static std::atomic<const std::string*> g_log_path{nullptr};
static std::atomic<bool> g_flush{false};
static std::atomic<bool> g_allow_flood{false};

namespace {
constexpr size_t   kRingLines = 4096;
constexpr size_t   kLineMax   = 1152;
constexpr int      kPublishSpin = 4096;

struct LogLine {
    std::atomic<uint64_t> seq{0};
    uint32_t              len        = 0;
    uint8_t               to_console = 0;
    uint8_t               to_file    = 0;
    char                  text[kLineMax];
};

LogLine               g_ring[kRingLines];
std::atomic<uint64_t> g_ring_w{0};
std::atomic<uint64_t> g_ring_r{0};
std::atomic<uint64_t> g_ring_dropped{0};
std::atomic<uint64_t> g_ring_written{0};
std::atomic<uint64_t> g_write_fail{0};
std::atomic<uint64_t> g_write_fail_bytes{0};
std::atomic<uint64_t> g_flush_giveup{0};
std::atomic<uint64_t> g_no_file_lines{0};
std::atomic<bool>     g_emergency_drained{false};
std::atomic<bool>     g_ring_inited{false};
host_platform::HostEvent* g_ring_event = nullptr;
std::once_flag        g_writer_once;

std::recursive_mutex& DrainMutex() {
    static std::recursive_mutex* m = new std::recursive_mutex;
    return *m;
}

void WriteLogFile(host_platform::HostFile* f, const char* buf, size_t len) {
    if (f == nullptr || len == 0) return;
    size_t done = 0;
    while (done < len) {
        uint32_t written = 0;
        if (!f->Write(buf + done, (uint32_t)(len - done), written) || written == 0) {
            g_write_fail.fetch_add(1, std::memory_order_relaxed);
            g_write_fail_bytes.fetch_add(len - done, std::memory_order_relaxed);
            return;
        }
        done += written;
    }
}

LogLine* RingAcquire(uint64_t* out_w) {
    for (;;) {
        const uint64_t w = g_ring_w.load(std::memory_order_relaxed);
        if (w - g_ring_r.load(std::memory_order_acquire) >= kRingLines) {
            g_ring_dropped.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        uint64_t expected = w;
        if (g_ring_w.compare_exchange_weak(expected, w + 1,
                                           std::memory_order_acq_rel,
                                           std::memory_order_relaxed)) {
            *out_w = w;
            return &g_ring[w % kRingLines];
        }
    }
}

void RingPublish(LogLine* e, uint64_t w, size_t len, bool to_console,
                 bool to_file) {
    e->len        = (uint32_t)len;
    e->to_console = to_console ? 1u : 0u;
    e->to_file    = to_file ? 1u : 0u;
    e->seq.store(w + 1, std::memory_order_release);
    if (w == g_ring_r.load(std::memory_order_relaxed)) {
        g_ring_event->Set();
    }
}

void ReportBothSinks(const char* msg, size_t n) {
    {
        std::lock_guard<std::recursive_mutex> lk(DrainMutex());
        WriteLogFile(g_log_file.load(std::memory_order_acquire), msg, n);
    }
    host_platform::WriteStdout(msg, n);
}

void DrainRing() {
    std::lock_guard<std::recursive_mutex> lk(DrainMutex());
    host_platform::HostFile* const log_f = g_log_file.load(std::memory_order_acquire);
    static char file_buf[65536];
    for (;;) {
        const uint64_t r0 = g_ring_r.load(std::memory_order_acquire);
        const uint64_t w  = g_ring_w.load(std::memory_order_acquire);
        size_t   file_n = 0;
        uint64_t r      = r0;
        bool     stalled = false;
        while (r < w) {
            LogLine& e = g_ring[r % kRingLines];
            bool published = false;
            for (int i = 0; i < kPublishSpin; ++i) {
                if (e.seq.load(std::memory_order_acquire) == r + 1) {
                    published = true;
                    break;
                }
                host_platform::CpuRelax();
            }
            if (!published) {
                stalled = true;
                break;
            }
            if (e.to_file) {
                if (log_f == nullptr) {
                    g_no_file_lines.fetch_add(1, std::memory_order_relaxed);
                } else {
                    if (file_n + e.len > sizeof(file_buf)) break;
                    memcpy(file_buf + file_n, e.text, e.len);
                    file_n += e.len;
                }
            }
            if (e.to_console) {
                host_platform::WriteStdout(e.text, e.len);
            }
            ++r;
        }
        if (r == r0) break;
        WriteLogFile(log_f, file_buf, file_n);
        g_ring_written.store(r, std::memory_order_release);
        for (uint64_t i = r0; i < r; ++i) {
            g_ring[i % kRingLines].seq.store(0, std::memory_order_release);
        }
        g_ring_r.store(r, std::memory_order_release);
        if (stalled || r == w) break;
    }
    const bool have_sink = log_f != nullptr || host_platform::StdoutAvailable();
    if (have_sink &&
        (g_ring_dropped.load(std::memory_order_relaxed) != 0 ||
         g_write_fail.load(std::memory_order_relaxed) != 0 ||
         g_flush_giveup.load(std::memory_order_relaxed) != 0 ||
         (log_f != nullptr &&
          g_no_file_lines.load(std::memory_order_relaxed) != 0))) {
        const unsigned long dropped = (unsigned long)g_ring_dropped.exchange(0);
        const unsigned long fails   = (unsigned long)g_write_fail.exchange(0);
        const unsigned long bytes =
            (unsigned long)g_write_fail_bytes.exchange(0);
        const unsigned long giveups =
            (unsigned long)g_flush_giveup.exchange(0);
        const unsigned long nofile =
            log_f != nullptr
                ? (unsigned long)g_no_file_lines.exchange(0)
                : 0ul;
        char msg[224];
        int n = snprintf(msg, sizeof(msg),
                         "[LOG] counters: %lu lines dropped, %lu "
                         "file writes failed (%lu bytes lost), %lu "
                         "flush requests deferred, %lu lines predate "
                         "the log file (console only)\n",
                         dropped, fails, bytes, giveups, nofile);
        if (n >= (int)sizeof(msg)) n = (int)sizeof(msg) - 1;
        if (n > 0) ReportBothSinks(msg, (size_t)n);
    }
}

void EmergencyDrainRing() {
    bool expected = false;
    if (!g_emergency_drained.compare_exchange_strong(expected, true)) return;
    const bool owned = DrainMutex().try_lock();
    uint64_t       r  = g_ring_r.load(std::memory_order_acquire);
    const uint64_t wm = g_ring_written.load(std::memory_order_acquire);
    if (wm > r) r = wm;
    const uint64_t w = g_ring_w.load(std::memory_order_acquire);
    uint64_t skipped = 0;
    uint64_t emitted = 0;
    for (uint64_t i = r; i < w; ++i) {
        LogLine& e = g_ring[i % kRingLines];
        if (e.seq.load(std::memory_order_acquire) != i + 1) {
            ++skipped;
            continue;
        }
        if (emitted == 0) {
            Log::Emergency("[LOG] ring tail (%lu slots pending at the crash):\n",
                           (unsigned long)(w - r));
        }
        ++emitted;
        Log::EmergencyWriteBytes(e.text, e.len);
    }
    const unsigned long dropped =
        (unsigned long)g_ring_dropped.load(std::memory_order_relaxed);
    const unsigned long wfails =
        (unsigned long)g_write_fail.load(std::memory_order_relaxed);
    const unsigned long wbytes =
        (unsigned long)g_write_fail_bytes.load(std::memory_order_relaxed);
    Log::Emergency("[LOG] emergency drain: %lu lines recovered here, %lu "
                   "unpublished (producer frozen mid-format), %lu dropped, "
                   "%lu file-write failures (%lu bytes)\n",
                   (unsigned long)emitted, (unsigned long)skipped, dropped,
                   wfails, wbytes);
    if (owned) DrainMutex().unlock();
}

void RingWriterThread() {
    for (;;) {
        g_ring_event->Wait(20);
        DrainRing();
    }
}

void EnsureRingWriter() {
    std::call_once(g_writer_once, []() {
        g_ring_event = new host_platform::HostEvent;
        bool started = g_ring_event->Create();
        if (started) {
            try {
                std::thread(RingWriterThread).detach();
            } catch (const std::system_error&) {
                started = false;
            }
        }
        if (!started) {
            const char msg[] =
                "CERF log: ring writer creation failed; exiting\n";
            host_platform::WriteStderr(msg, sizeof(msg) - 1);
            host_platform::ExitProcessNow(CERF_FATAL_RUNTIME_ERROR);
        }
        atexit([] { Log::Close(); });
        g_ring_inited.store(true, std::memory_order_release);
    });
}

uint32_t NowMs32() {
    return (uint32_t)(HostSteadyMicros() / 1000);
}
}

static constexpr int      FLOOD_THRESHOLD    = 50;
static constexpr uint32_t FLOOD_WINDOW_MS    = 1000;
static constexpr uint32_t FLOOD_SUPPRESS_MS  = 5000;

struct FloodState {
    uint32_t window_start;
    int      count;
    uint32_t suppress_until;
    bool     announced;
};
static FloodState g_flood[(size_t)Log::Cat::COUNT] = {};

static bool IsFloodSuppressed(Log::Cat cat) {
    if (Log::IsAlwaysOn(cat)) return false;
    if (g_allow_flood) return false;
    int idx = (int)cat;
    if (idx < 0 || idx >= (int)Log::Cat::COUNT) return false;

    FloodState& fs = g_flood[idx];
    uint32_t now = NowMs32();

    if (fs.suppress_until && (int)(now - fs.suppress_until) < 0) {
        return true;
    }

    if (fs.suppress_until && (int)(now - fs.suppress_until) >= 0) {
        fs.suppress_until = 0;
        fs.announced = false;
        fs.count = 0;
        fs.window_start = now;
    }

    if ((int)(now - fs.window_start) > (int)FLOOD_WINDOW_MS) {
        fs.window_start = now;
        fs.count = 0;
    }
    fs.count++;

    if (fs.count > FLOOD_THRESHOLD) {
        fs.suppress_until = now + FLOOD_SUPPRESS_MS;
        if (!fs.announced) {
            uint64_t w  = 0;
            LogLine* e  = RingAcquire(&w);
            if (e != nullptr) {
                fs.announced = true;
                int n = snprintf(
                    e->text, kLineMax,
                    "[LOG] Category %s suppressed on stdout (flood: %d "
                    "lines/sec). Log file unaffected.\n",
                    Log::kCategories[idx].slug, fs.count);
                if (n < 0) n = 0;
                if ((size_t)n >= kLineMax) n = (int)kLineMax - 1;
                RingPublish(e, w, (size_t)n, true, false);
            }
        }
        return true;
    }
    return false;
}

void Log::InitDefaultLogFile() {
    if (g_log_file.load(std::memory_order_acquire) != nullptr) {
        return;
    }
    const std::string dir = GetCerfDir();
    if (dir.empty()) return;
    SetFile((dir + "cerf.log").c_str());
}

void Log::SetEnabled(uint64_t mask) {
    detail::enabled_mask.store(mask, std::memory_order_relaxed);
}
void Log::SetAllowFlood(bool allow) { g_allow_flood = allow; }

void Log::SetFile(const char* path) {
    EnsureRingWriter();
    bool     opened = false;
    uint32_t error  = 0;
    {
        std::lock_guard<std::recursive_mutex> lk(DrainMutex());
        auto* f = new host_platform::HostFile;
        if (f->Open(path, host_platform::FileMode::kWriteNewShared)) {
            host_platform::HostFile* const old =
                g_log_file.exchange(f, std::memory_order_acq_rel);
            delete old;
            g_log_path.store(new std::string(path), std::memory_order_release);
            opened = true;
        } else {
            error = host_platform::LastErrorCode();
            delete f;
        }
    }
    if (!opened) {
        fprintf(stderr, "Warning: could not open log file '%s' (gle=%lu)\n",
                path, (unsigned long)error);
    }
}

const char* Log::FilePath() {
    const std::string* path = g_log_path.load(std::memory_order_acquire);
    return path != nullptr ? path->c_str() : "";
}

void Log::SetFlush(bool enabled) {
    g_flush = enabled;
}

void Log::Close() {
    if (IsEmergencyActive()) {
        if (g_ring_inited.load(std::memory_order_acquire)) {
            EmergencyDrainRing();
        }
        return;
    }
    EnsureRingWriter();
    const uint64_t w = g_ring_w.load(std::memory_order_acquire);
    for (int i = 0;
         i < 64 && g_ring_r.load(std::memory_order_acquire) < w; ++i) {
        DrainRing();
    }
    const uint64_t r = g_ring_r.load(std::memory_order_acquire);
    if (r < w) {
        char msg[128];
        int n = snprintf(msg, sizeof(msg),
                         "[LOG] %lu lines unpublished at close\n",
                         (unsigned long)(w - r));
        if (n >= (int)sizeof(msg)) n = (int)sizeof(msg) - 1;
        if (n > 0) ReportBothSinks(msg, (size_t)n);
    }
    fflush(stdout);
    fflush(stderr);
}

static int64_t        g_tick_freq  = 0;
static int64_t        g_tick_epoch = 0;
static std::once_flag g_epoch_once;

static void EnsureTickEpoch() {
    std::call_once(g_epoch_once, []() {
        g_tick_freq  = host_platform::MonotonicFrequency();
        g_tick_epoch = host_platform::MonotonicTicks();
    });
}

static int PrefixToBuf(char* buf, size_t cap, uint32_t tid, const char* slug) {
    EnsureTickEpoch();
    const int64_t now = host_platform::MonotonicTicks();
    double elapsed = (double)(now - g_tick_epoch) / (double)g_tick_freq;
    return snprintf(buf, cap, "[t+%.6fs][T%lu] [%s] ", elapsed, (unsigned long)tid, slug);
}

void Log::Print(Cat cat, const char* fmt, ...) {
    if (!IsEnabled(cat)) return;
    va_list args;
    va_start(args, fmt);
    const uint32_t tid = host_platform::CurrentThreadId();
    EnsureRingWriter();

    const char* slug = kCategories[(int)cat].slug;

    bool to_console;
    {
        std::lock_guard<std::mutex> lk(log_detail::LogMutex());
        if (cat == Cat::Caution) {
            va_list args_ring;
            va_copy(args_ring, args);
            log_detail::RecordCautionLocked(fmt, args_ring);
            va_end(args_ring);
        }
        to_console = !IsFloodSuppressed(cat);
    }

    uint64_t w = 0;
    LogLine* e = RingAcquire(&w);
    if (e == nullptr) {
        va_end(args);
        return;
    }
    int n = PrefixToBuf(e->text, kLineMax, tid, slug);
    if (n < 0) n = 0;
    if ((size_t)n >= kLineMax) n = (int)kLineMax - 1;
    int m = vsnprintf(e->text + n, kLineMax - (size_t)n, fmt, args);
    va_end(args);
    if (m < 0) m = 0;
    size_t len = (size_t)n + (size_t)m;
    if (len >= kLineMax) {
        len = kLineMax - 1;
        memcpy(&e->text[kLineMax - 6], " ...\n", 5);
    }
    RingPublish(e, w, len, to_console, true);

    if (g_flush.load(std::memory_order_relaxed)) {
        for (int i = 0;
             i < 64 && g_ring_r.load(std::memory_order_acquire) <= w; ++i) {
            DrainRing();
        }
        if (g_ring_r.load(std::memory_order_acquire) <= w) {
            g_flush_giveup.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

}
