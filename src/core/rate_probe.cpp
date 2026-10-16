#include "rate_probe.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "timestamp_counter.h"

#include <chrono>
#include <cstdio>

REGISTER_SERVICE(RateProbe);

RateProbe::~RateProbe() {
    stop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(cv_mtx_);
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void RateProbe::CalibrateTscPerSec() {
    using namespace std::chrono;
    const uint64_t tsc0 = ReadTimestampCounter();
    const auto     w0   = steady_clock::now();
    std::this_thread::sleep_for(milliseconds(50));
    const uint64_t tsc1 = ReadTimestampCounter();
    const auto     w1   = steady_clock::now();
    const uint64_t wall_ns = static_cast<uint64_t>(
        duration_cast<nanoseconds>(w1 - w0).count());
    tsc_per_sec_ = (wall_ns == 0) ? 1ull
                                  : ((tsc1 - tsc0) * 1'000'000'000ull) / wall_ns;
    LOG(Perf, "TSC calibrated: %llu ticks/sec (~%llu MHz)\n",
        (unsigned long long)tsc_per_sec_,
        (unsigned long long)(tsc_per_sec_ / 1'000'000ull));
}

void RateProbe::OnReady() {
#if CERF_DEV_MODE
    CalibrateTscPerSec();
    thread_ = std::thread([this] { LogLoop(); });
#endif
}

void RateProbe::LogTopMmioPcs() {
    struct Entry { uint32_t pc; uint32_t addr; uint64_t count; };
    Entry snap[kMmioHistSize];
    uint32_t live = 0;
    for (uint32_t i = 0; i < kMmioHistSize; ++i) {
        const uint64_t c = mmio_count_[i].exchange(0, std::memory_order_relaxed);
        if (c == 0) continue;
        snap[live].pc    = mmio_pc_[i].load(std::memory_order_relaxed);
        snap[live].addr  = mmio_addr_[i].load(std::memory_order_relaxed);
        snap[live].count = c;
        ++live;
    }
    /* Partial sort top 10 by count. */
    constexpr uint32_t kTop = 10;
    const uint32_t n = (live < kTop) ? live : kTop;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t best = i;
        for (uint32_t j = i + 1; j < live; ++j) {
            if (snap[j].count > snap[best].count) best = j;
        }
        if (best != i) {
            Entry tmp = snap[i]; snap[i] = snap[best]; snap[best] = tmp;
        }
        LOG(Perf, "  mmio_pc top%u: pc=0x%08X addr=0x%08X count=%llu\n",
            i + 1, snap[i].pc, snap[i].addr,
            (unsigned long long)snap[i].count);
    }
}

void RateProbe::LogTopCtxSlots() {
    struct Entry { uint32_t slot; uint64_t count; };
    Entry snap[128];
    uint32_t live = 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < 128; ++i) {
        const uint64_t c = ctx_slot_[i].exchange(0, std::memory_order_relaxed);
        if (c == 0) continue;
        snap[live].slot = i;
        snap[live].count = c;
        ++live;
        total += c;
    }
    if (total == 0) return;
    constexpr uint32_t kTop = 4;
    const uint32_t n = (live < kTop) ? live : kTop;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t best = i;
        for (uint32_t j = i + 1; j < live; ++j) {
            if (snap[j].count > snap[best].count) best = j;
        }
        if (best != i) { Entry t = snap[i]; snap[i] = snap[best]; snap[best] = t; }
        LOG(Perf, "  ctx_slot top%u: slot=%u (pid=0x%08X) switches=%llu\n",
            i + 1, snap[i].slot, snap[i].slot << 25,
            (unsigned long long)snap[i].count);
    }
}

void RateProbe::LogLoop() {
    using namespace std::chrono;
    auto next_tick = steady_clock::now() + seconds(1);

    /* Per-counter total since boot; a lifetime of 0 means the Inc() site is
       not wired on this board, distinct from an idle 0 this interval. */
    uint64_t lifetime[kCount] = {};
    uint32_t prev[kCount] = {};
    uint64_t prev_time[kTimeCount] = {};

    while (!stop_.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lk(cv_mtx_);
            cv_.wait_until(lk, next_tick, [this] {
                return stop_.load(std::memory_order_acquire);
            });
        }
        if (stop_.load(std::memory_order_acquire)) break;
        next_tick += seconds(1);

        uint64_t s[kCount];
        for (uint8_t i = 0; i < kCount; ++i) {
            const uint32_t cur = counters_[i].load(std::memory_order_relaxed);
            s[i] = static_cast<uint32_t>(cur - prev[i]);
            prev[i] = cur;
        }
        uint64_t t[kTimeCount];
        for (uint8_t i = 0; i < kTimeCount; ++i) {
            const uint64_t cur =
                time_counters_[i].load(std::memory_order_relaxed);
            t[i] = cur - prev_time[i];
            prev_time[i] = cur;
        }
        for (uint8_t i = 0; i < kCount; ++i) lifetime[i] += s[i];

        const uint64_t rd = s[static_cast<uint8_t>(Counter::OstReadOscr)];
        const uint64_t jr = s[static_cast<uint8_t>(Counter::JitRuns)];
        const uint64_t rd_per_run = jr ? rd / jr : 0;
        const uint64_t ticks_per_ms = tsc_per_sec_ / 1000ull;
        const uint64_t run_ms = ticks_per_ms ?
            t[static_cast<uint8_t>(TimeCounter::JitRun)]   / ticks_per_ms : 0;
        const uint64_t ost_ms = ticks_per_ms ?
            t[static_cast<uint8_t>(TimeCounter::OstMmio)]  / ticks_per_ms : 0;
        const uint64_t io_ms  = ticks_per_ms ?
            t[static_cast<uint8_t>(TimeCounter::JitIo)]    / ticks_per_ms : 0;
        const uint64_t mmu_ms = ticks_per_ms ?
            t[static_cast<uint8_t>(TimeCounter::MmuXlate)] / ticks_per_ms : 0;
        const uint64_t native_ms =
            (run_ms > io_ms + mmu_ms) ? run_ms - io_ms - mmu_ms : 0;
#if CERF_DEV_MODE
        {
            const bool active = jr != 0;
            if (!active && sd_active_prev_ && sd_fired_ < 2u) {
                ++sd_fired_;
                LOG(Perf, "[JITSTALLDUMP] jit_runs fell to 0 after an active "
                          "second - invoking the registered stall dumps "
                          "(#%u)\n",
                    sd_fired_);
                std::vector<std::function<void()>> fns;
                {
                    std::lock_guard<std::mutex> lk(sd_mtx_);
                    fns = stall_dumps_;
                }
                for (const auto& fn : fns) fn();
            }
            sd_active_prev_ = active;
        }
#endif
        LogTopMmioPcs();
        LogTopCtxSlots();
        char cbuf[kCount][24];
        for (uint8_t i = 0; i < kCount; ++i) {
            if (lifetime[i] == 0) {
                std::snprintf(cbuf[i], sizeof(cbuf[i]), "(not wired)");
            } else {
                std::snprintf(cbuf[i], sizeof(cbuf[i]), "%llu",
                              (unsigned long long)s[i]);
            }
        }
        LOG(Perf,
            "jit_runs=%s ost_rd=%s ost_fire=%s "
            "intc_assert=%s intc_deassert=%s jit_pend_set=%s "
            "jit_pend_clr=%s dma_w=%s audio_msg=%s rd_per_run=%llu "
            "run_ms=%llu ost_ms=%llu io_ms=%llu mmu_ms=%llu native_ms=%llu "
            "mmu_calls=%s jit_compile=%s tc_flush=%s ctx_flush=%s\n",
            cbuf[static_cast<uint8_t>(Counter::JitRuns)],
            cbuf[static_cast<uint8_t>(Counter::OstReadOscr)],
            cbuf[static_cast<uint8_t>(Counter::OstFires)],
            cbuf[static_cast<uint8_t>(Counter::IntcAsserts)],
            cbuf[static_cast<uint8_t>(Counter::IntcDeasserts)],
            cbuf[static_cast<uint8_t>(Counter::JitPendSet)],
            cbuf[static_cast<uint8_t>(Counter::JitPendClr)],
            cbuf[static_cast<uint8_t>(Counter::DmaWrites)],
            cbuf[static_cast<uint8_t>(Counter::AudioMsgs)],
            (unsigned long long)rd_per_run,
            (unsigned long long)run_ms,
            (unsigned long long)ost_ms,
            (unsigned long long)io_ms,
            (unsigned long long)mmu_ms,
            (unsigned long long)native_ms,
            cbuf[static_cast<uint8_t>(Counter::MmuXlateCalls)],
            cbuf[static_cast<uint8_t>(Counter::JitCompiles)],
            cbuf[static_cast<uint8_t>(Counter::TcFlushes)],
            cbuf[static_cast<uint8_t>(Counter::CtxFlushes)]);
    }
}
