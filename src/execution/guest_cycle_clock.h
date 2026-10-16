#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <vector>

#include "../core/emulation_speed_config.h"
#include <cerfcore/core/force_inline.h>
#include <cerfcore/core/service.h>
#include "../core/tick_scale.h"
#include "../platform/host_wait_timer.h"
#include <cerfengine/clock_host.h>

#if CERF_DEV_MODE
#include <atomic>
#endif

namespace cerfcore {

class VirtualClock;

class GuestCycleClock : public cerfengine::ClockHost, public Service {
public:
    using Service::Service;

    class Event {
    public:
        explicit Event(std::function<void()> fn) : fn_(std::move(fn)) {}

    private:
        friend class GuestCycleClock;
        std::function<void()> fn_;
        uint64_t              at_    = 0;
        bool                  armed_ = false;
    };

    struct Rate {
        uint64_t num = 0;
        uint64_t den = 1;

        bool operator==(const Rate&) const = default;

        bool ScaleFits(uint64_t mul, uint64_t div) const {
            return (mul == 0u || num <= UINT64_MAX / mul) && den <= UINT64_MAX / div;
        }
        Rate Scaled(uint64_t mul, uint64_t div) const { return Rate{num * mul, den * div}; }
        Rate Reduced() const {
            const uint64_t g = std::gcd(num, den);
            return Rate{num / g, den / g};
        }
        uint64_t RoundedHz() const { return (num + den / 2u) / den; }
    };

    void OnReady() override;

    Event* Add(std::function<void()> fn);
    void   Arm(Event* e, uint64_t at_cycle);
    void   Disarm(Event* e);
    bool   IsDue(const Event* e, uint64_t now) const;

    uint64_t CpuHz() const;
    Rate     ClockRate() const { return rate_; }
    void     SetClockHz(uint64_t hz) { SetClockRate(Rate{hz, 1u}); }
    void     SetClockRate(Rate rate);
    void     RegisterRateListener(std::function<void()> fn);
    void     RegisterIdleListener(std::function<void()> fn);
    void     RegisterIdleExitListener(std::function<void()> fn);
    int64_t  NowNs() { return guest_ns_ + CyclesToNs(Cycles() - guest_ns_cycle_); }
    uint64_t NsToCycles(int64_t ns) const;
    int64_t  CyclesToNs(uint64_t cycles) const;

    void OnDispatch();
    void IdleStep(void* wake_event) final;
    void WaitStep(void* wake_event) final;
    void ExitIdle() final;
    bool InIdle() const { return in_idle_; }
    void OnCyclesRestored();

#if CERF_DEV_MODE
    int64_t LagNsAtLastSecond() const {
        return stat_lag_published_ns_.load(std::memory_order_relaxed);
    }
#endif

protected:
    virtual Rate     InitialRate()                      = 0;
    virtual void     SetCycles(uint64_t cycles)         = 0;
    virtual void     PublishDeadline(uint64_t cycles_ahead) = 0;

private:
    friend void CERFENGINE_FASTCALL cerfengine::host::ClockDispatch(cerfengine::ClockHost* clock);

    CERF_FORCEINLINE void DispatchDue();

    Rate     Normalized(Rate rate) const;
    Rate     WithCpuMultiplier(Rate rate) const;
    void     LogRate() const;
    uint64_t WallToCycles(int64_t wall_ns) const;
    void     SetUnits(Rate rate);
    void     RunDue(uint64_t now);
    uint64_t NextArmed() const;
    void     Publish(uint64_t now);
    void     OnThrottle();
    int64_t  TargetWallNs(uint64_t cycle) const;
    void     SleepUntilWallNs(int64_t target_ns);

    std::vector<std::unique_ptr<Event>> events_;
    std::vector<std::function<void()>>  rate_listeners_;
    std::vector<std::function<void()>>  idle_listeners_;
    std::vector<std::function<void()>>  idle_exit_listeners_;
    bool          in_idle_     = false;
    Event*        throttle_    = nullptr;
    VirtualClock* wall_        = nullptr;
    host_platform::HostWaitTimer timer_;
    Rate          rate_        = Rate{1u, 1u};
    uint64_t      rate_requests_ = 0;
    ExactScale    ns_to_cycles_;
    ExactScale    cycles_to_ns_;
    uint64_t      ref_cycle_   = 0;
    int64_t       ref_wall_ns_ = 0;
    uint64_t      guest_ns_cycle_ = 0;
    int64_t       guest_ns_       = 0;
    EmulationSpeedConfig::Multiplier throttle_scale_;
    EmulationSpeedConfig::Multiplier cpu_rate_scale_;

#if CERF_DEV_MODE
    void LogSecond();
    uint32_t stat_throttles_    = 0;
    uint32_t stat_sleeps_       = 0;
    int64_t  stat_sleep_ns_     = 0;
    int64_t  stat_wake_over_ns_ = 0;
    int64_t  stat_stall_ns_     = 0;
    int64_t  stat_prev_lag_ns_  = 0;
    int64_t  stat_forgiven_ns_  = 0;
    uint32_t stat_idle_steps_   = 0;
    uint32_t stat_idle_early_   = 0;
    int64_t  stat_idle_wait_ns_ = 0;
    int64_t  stat_wall_mark_ns_ = 0;
    int64_t  stat_guest_mark_ns_ = 0;
    std::atomic<int64_t> stat_lag_published_ns_{0};
#endif
};

}
