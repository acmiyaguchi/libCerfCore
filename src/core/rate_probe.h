#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#if CERF_DEV_MODE
#include <functional>
#include <vector>
#endif

class RateProbe : public Service {
public:
    using Service::Service;
    ~RateProbe() override;

    void OnReady() override;

#if CERF_DEV_MODE
    void RegisterStallDump(std::function<void()> fn) {
        std::lock_guard<std::mutex> lk(sd_mtx_);
        stall_dumps_.push_back(std::move(fn));
    }
#endif

    enum class Counter : uint8_t {
        JitRuns         = 0,
        OstReadOscr,
        OstFires,
        IntcAsserts,
        IntcDeasserts,
        JitPendSet,
        JitPendClr,
        DmaWrites,
        AudioMsgs,
        MmuXlateCalls,
        JitCompiles,      /* block translations/sec - high = TC thrash / cold cache. */
        TcFlushes,        /* full translation-cache flushes/sec (FlushAll+arena). */
        CtxFlushes,       /* context-switch jump-cache flushes/sec (FCSE PID switch). */
        Count,
    };

    /* TSC-tick accumulators. Wallclock cost of code paths the JIT thread
       executes; emitted as estimated ms/sec in the PERF line. */
    enum class TimeCounter : uint8_t {
        JitRun          = 0,
        OstMmio,
        JitIo,
        MmuXlate,
        Count,
    };

    static constexpr uint32_t kMmioHistSize = 2048;

#if CERF_DEV_MODE
    void Inc(Counter c) {
        counters_[static_cast<uint8_t>(c)].fetch_add(1u,
                                                     std::memory_order_relaxed);
    }

    void AddTsc(TimeCounter c, uint64_t ticks) {
        time_counters_[static_cast<uint8_t>(c)]
            .fetch_add(ticks, std::memory_order_relaxed);
    }

    /* Per-FCSE-slot context-switch histogram. process_id bits[31:25] = the
       process slot (0..127); the dumper prints the busiest slots so a
       context-switch storm names the ping-ponging processes. */
    void RecordCtxSlot(uint32_t process_id) {
        ctx_slot_[(process_id >> 25) & 0x7Fu].fetch_add(1, std::memory_order_relaxed);
    }

    /* Per-guest-PC MMIO call histogram. Linear-probe open-addressed hash;
       relaxed atomics throughout - slot races at edge of buckets cause at
       most a few miscounts, fine for sampling. */
    void RecordMmioPc(uint32_t guest_pc, uint32_t addr) {
        const uint32_t mix = guest_pc ^ (addr * 2654435761u);
        for (uint32_t probe = 0; probe < 16; ++probe) {
            const uint32_t slot = (mix + probe) & (kMmioHistSize - 1);
            const uint32_t cur_pc = mmio_pc_[slot].load(std::memory_order_relaxed);
            if (cur_pc == guest_pc &&
                mmio_addr_[slot].load(std::memory_order_relaxed) == addr) {
                mmio_count_[slot].fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (cur_pc == 0) {
                uint32_t expected = 0;
                if (mmio_pc_[slot].compare_exchange_strong(
                        expected, guest_pc, std::memory_order_relaxed)) {
                    mmio_addr_[slot].store(addr, std::memory_order_relaxed);
                    mmio_count_[slot].fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                if (mmio_pc_[slot].load(std::memory_order_relaxed) == guest_pc &&
                    mmio_addr_[slot].load(std::memory_order_relaxed) == addr) {
                    mmio_count_[slot].fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
        }
        /* Exhausted probes - drop the sample. */
    }
#else
    void Inc(Counter)                    {}
    void AddTsc(TimeCounter, uint64_t)   {}
    void RecordMmioPc(uint32_t, uint32_t){}
    void RecordCtxSlot(uint32_t)         {}
#endif

private:
    void LogLoop();
    void LogTopMmioPcs();
    void LogTopCtxSlots();
    void CalibrateTscPerSec();

    static constexpr uint8_t kCount     = static_cast<uint8_t>(Counter::Count);
    static constexpr uint8_t kTimeCount = static_cast<uint8_t>(TimeCounter::Count);

    std::atomic<uint32_t>   counters_[kCount]{};
    std::atomic<uint64_t>   time_counters_[kTimeCount]{};
    std::atomic<uint32_t>   mmio_pc_[kMmioHistSize]{};
    std::atomic<uint32_t>   mmio_addr_[kMmioHistSize]{};
    std::atomic<uint64_t>   mmio_count_[kMmioHistSize]{};
    std::atomic<uint64_t>   ctx_slot_[128]{};
    uint64_t                tsc_per_sec_{1};
    std::atomic<bool>       stop_{false};
    std::thread             thread_;
    std::mutex              cv_mtx_;
    std::condition_variable cv_;
#if CERF_DEV_MODE
    std::mutex                         sd_mtx_;
    std::vector<std::function<void()>> stall_dumps_;
    bool                               sd_active_prev_ = false;
    uint32_t                           sd_fired_       = 0;
#endif
};
