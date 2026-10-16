#include "guest_cycle_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../core/tick_scale.h"
#include "../core/virtual_clock.h"
#include "engine_bridge.h"
#include "../platform/host_error.h"

#include <algorithm>
#include <numeric>

namespace cerfcore {

namespace {

constexpr uint64_t kNsPerSec        = 1000000000ull;
constexpr int64_t  kThrottleSliceNs = 1000000;
constexpr int64_t  kMaxCatchUpNs    = 100000000;
constexpr uint64_t kNever           = UINT64_MAX;

}

GuestCycleClock::Rate GuestCycleClock::Normalized(Rate rate) const {
    if (rate.num == 0u || rate.den == 0u) {
        emu_.Get<Fatal>().Die("GuestCycleClock: the clock rate %llu/%llu Hz",
                              static_cast<unsigned long long>(rate.num),
                              static_cast<unsigned long long>(rate.den));
    }
    return rate.Reduced();
}

GuestCycleClock::Rate GuestCycleClock::WithCpuMultiplier(Rate rate) const {
    if (cpu_rate_scale_.IsOne()) return rate;
    if (!rate.ScaleFits(cpu_rate_scale_.num, cpu_rate_scale_.den)) {
        emu_.Get<Fatal>().Die("GuestCycleClock: the clock rate %llu/%llu Hz times the CPU rate "
                              "multiplier %llu/%llu does not fit 64 bits",
                              static_cast<unsigned long long>(rate.num),
                              static_cast<unsigned long long>(rate.den),
                              static_cast<unsigned long long>(cpu_rate_scale_.num),
                              static_cast<unsigned long long>(cpu_rate_scale_.den));
    }
    Rate scaled = rate.Scaled(cpu_rate_scale_.num, cpu_rate_scale_.den).Reduced();
    if (rate.den == 1u && scaled.den != 1u) scaled = Rate{scaled.RoundedHz(), 1u};
    return Normalized(scaled);
}

void GuestCycleClock::LogRate() const {
    LOG(Jit, "[CYCLECLK] rate %llu/%llu Hz, CPU rate multiplier %llu/%llu, throttle multiplier "
             "%llu/%llu\n",
        static_cast<unsigned long long>(rate_.num), static_cast<unsigned long long>(rate_.den),
        static_cast<unsigned long long>(cpu_rate_scale_.num),
        static_cast<unsigned long long>(cpu_rate_scale_.den),
        static_cast<unsigned long long>(throttle_scale_.num),
        static_cast<unsigned long long>(throttle_scale_.den));
}

uint64_t GuestCycleClock::WallToCycles(int64_t wall_ns) const {
    if (throttle_scale_.num == 0u) return kNever;
    return NsToCycles(static_cast<int64_t>(MulDivU64(static_cast<uint64_t>(wall_ns),
                                                     throttle_scale_.den, throttle_scale_.num)));
}

uint64_t GuestCycleClock::CpuHz() const {
    if (rate_.den != 1u) {
        emu_.Get<Fatal>().Die("GuestCycleClock: an integral Hz read of the rate %llu/%llu Hz",
                              static_cast<unsigned long long>(rate_.num),
                              static_cast<unsigned long long>(rate_.den));
    }
    return rate_.num;
}

void GuestCycleClock::SetUnits(Rate rate) {
    rate_ = rate;
    const uint64_t    g      = std::gcd(kNsPerSec, rate_.num);
    const WideUint<2> cycles = WideUint<2>::Of(rate_.num / g);
    const WideUint<2> ns =
        MultiplyWide(WideUint<1>::Of(kNsPerSec / g), WideUint<1>::Of(rate_.den));
    ns_to_cycles_.Set(cycles, ns);
    cycles_to_ns_.Set(ns, cycles);
}

void GuestCycleClock::SetClockRate(Rate rate) {
    const Rate     r       = WithCpuMultiplier(Normalized(rate));
    const uint64_t request = ++rate_requests_;
    if (r == rate_) return;
    const uint64_t now = Cycles();
    RunDue(now);
    if (rate_requests_ != request) return;
    const int64_t slice_left_ns =
        throttle_->armed_ ? CyclesToNs(throttle_->at_ - now) : kThrottleSliceNs;
    ref_wall_ns_ = TargetWallNs(now);
    ref_cycle_   = now;
    guest_ns_      += CyclesToNs(now - guest_ns_cycle_);
    guest_ns_cycle_ = now;
    SetUnits(r);
    LogRate();
    Arm(throttle_, now + NsToCycles(slice_left_ns));
    for (auto& fn : rate_listeners_) fn();
}

void GuestCycleClock::RegisterRateListener(std::function<void()> fn) {
    rate_listeners_.push_back(std::move(fn));
}

void GuestCycleClock::RegisterIdleListener(std::function<void()> fn) {
    idle_listeners_.push_back(std::move(fn));
}

void GuestCycleClock::RegisterIdleExitListener(std::function<void()> fn) {
    idle_exit_listeners_.push_back(std::move(fn));
}

void GuestCycleClock::ExitIdle() {
    if (!in_idle_) return;
    in_idle_ = false;
    for (auto& fn : idle_exit_listeners_) fn();
}

void GuestCycleClock::OnReady() {
    const auto& speed = emu_.Get<EmulationSpeedConfig>();
    throttle_scale_   = speed.Throttle();
    cpu_rate_scale_   = speed.CpuRate();
    SetUnits(WithCpuMultiplier(Normalized(InitialRate())));
    LogRate();
    wall_ = &emu_.Get<VirtualClock>();
    if (!timer_.Create()) {
        emu_.Get<Fatal>().Die("GuestCycleClock: waitable timer creation failed gle=%u",
                              host_platform::LastErrorCode());
    }
    throttle_ = Add([this] { OnThrottle(); });
    OnCyclesRestored();
    emu_.Get<EngineBridge>().Engine().BindClock(*this);
}

uint64_t GuestCycleClock::NsToCycles(int64_t ns) const {
    return ns_to_cycles_.Floor(static_cast<uint64_t>(ns));
}

int64_t GuestCycleClock::CyclesToNs(uint64_t cycles) const {
    return static_cast<int64_t>(cycles_to_ns_.Floor(cycles));
}

GuestCycleClock::Event* GuestCycleClock::Add(std::function<void()> fn) {
    events_.push_back(std::make_unique<Event>(std::move(fn)));
    return events_.back().get();
}

void GuestCycleClock::Arm(Event* e, uint64_t at_cycle) {
    e->at_    = at_cycle;
    e->armed_ = true;
    Publish(Cycles());
}

void GuestCycleClock::Disarm(Event* e) {
    e->armed_ = false;
    Publish(Cycles());
}

bool GuestCycleClock::IsDue(const Event* e, uint64_t now) const {
    return e->armed_ && e->at_ <= now;
}

uint64_t GuestCycleClock::NextArmed() const {
    uint64_t next = kNever;
    for (const auto& e : events_) {
        if (e->armed_ && e->at_ < next) next = e->at_;
    }
    return next;
}

void GuestCycleClock::Publish(uint64_t now) {
    const uint64_t next = NextArmed();
    PublishDeadline(next > now ? next - now : 0u);
}

void GuestCycleClock::RunDue(uint64_t now) {
    for (;;) {
        Event* due = nullptr;
        for (const auto& e : events_) {
            if (e->armed_ && e->at_ <= now && (due == nullptr || e->at_ < due->at_)) {
                due = e.get();
            }
        }
        if (due == nullptr) return;
        due->armed_ = false;
        due->fn_();
    }
}

CERF_FORCEINLINE void GuestCycleClock::DispatchDue() {
    const uint64_t now = Cycles();
    RunDue(now);
    Publish(now);
}

void GuestCycleClock::OnDispatch() {
    DispatchDue();
}

int64_t GuestCycleClock::TargetWallNs(uint64_t cycle) const {
    const auto guest_ns = static_cast<uint64_t>(CyclesToNs(cycle - ref_cycle_));
    return ref_wall_ns_ +
           static_cast<int64_t>(MulDivU64(guest_ns, throttle_scale_.num, throttle_scale_.den));
}

void GuestCycleClock::SleepUntilWallNs(int64_t target_ns) {
    for (;;) {
        const int64_t remaining = target_ns - wall_->NowNs();
        if (remaining <= 0) return;
        timer_.Arm(remaining);
        timer_.Wait();
    }
}

void GuestCycleClock::OnThrottle() {
    const uint64_t now    = Cycles();
    const int64_t  target = TargetWallNs(now);
    const int64_t  wall   = wall_->NowNs();
    if (target > wall) {
        SleepUntilWallNs(target);
#if CERF_DEV_MODE
        ++stat_sleeps_;
        stat_sleep_ns_ += target - wall;
        stat_wake_over_ns_ += wall_->NowNs() - target;
        stat_prev_lag_ns_ = 0;
#endif
    } else {
#if CERF_DEV_MODE
        const int64_t grown = (wall - target) - stat_prev_lag_ns_;
        if (grown > 0) stat_stall_ns_ += grown;
#endif
        if (wall - target > kMaxCatchUpNs) {
            ref_wall_ns_ += (wall - target) - kMaxCatchUpNs;
#if CERF_DEV_MODE
            stat_forgiven_ns_ += (wall - target) - kMaxCatchUpNs;
#endif
        }
#if CERF_DEV_MODE
        stat_prev_lag_ns_ = std::min<int64_t>(wall - target, kMaxCatchUpNs);
#endif
    }
#if CERF_DEV_MODE
    if (++stat_throttles_ % 1000u == 0u) LogSecond();
#endif
    Arm(throttle_, now + NsToCycles(kThrottleSliceNs));
}

void GuestCycleClock::IdleStep(void* wake_event) {
    in_idle_ = true;
    for (auto& fn : idle_listeners_) fn();
    WaitStep(wake_event);
}

void GuestCycleClock::WaitStep(void* wake_event) {
    const uint64_t now  = Cycles();
    const uint64_t next = NextArmed();
    if (next == kNever) {
        emu_.Get<Fatal>().Die("GuestCycleClock: idle with no armed event");
    }
    if (next <= now) {
        RunDue(now);
        Publish(now);
        return;
    }
    const int64_t target = TargetWallNs(next);
    int64_t        wall   = wall_->NowNs();
    if (target > wall) {
        timer_.Arm(target - wall);
        timer_.WaitOrEvent(wake_event);
        timer_.Cancel();
#if CERF_DEV_MODE
        stat_idle_wait_ns_ += target - wall;
#endif
        wall = wall_->NowNs();
    }
    uint64_t advanced = next;
    if (wall < target) {
        const int64_t elapsed = wall - TargetWallNs(now);
        advanced = elapsed > 0 ? now + std::min(next - now, WallToCycles(elapsed)) : now;
#if CERF_DEV_MODE
        ++stat_idle_early_;
#endif
    }
#if CERF_DEV_MODE
    ++stat_idle_steps_;
#endif
    SetCycles(advanced);
    RunDue(advanced);
    Publish(advanced);
}

#if CERF_DEV_MODE
void GuestCycleClock::LogSecond() {
    const uint64_t now      = Cycles();
    const int64_t  wall     = wall_->NowNs();
    const int64_t  guest_ns = NowNs();
    LOG(Jit, "[CYCLECLK] guest_s=%llu guest_ms=%lld wall_ms=%lld lag_ms=%lld | "
             "sleeps=%u sleep_ms=%lld wake_over_us=%lld stall_us=%lld "
             "forgiven_ms=%lld | idle_steps=%u early=%u idle_wait_ms=%lld\n",
        static_cast<unsigned long long>(stat_throttles_ / 1000u),
        static_cast<long long>((guest_ns - stat_guest_mark_ns_) / 1000000),
        static_cast<long long>((wall - stat_wall_mark_ns_) / 1000000),
        static_cast<long long>((wall - TargetWallNs(now)) / 1000000),
        stat_sleeps_, static_cast<long long>(stat_sleep_ns_ / 1000000),
        static_cast<long long>(stat_wake_over_ns_ / 1000),
        static_cast<long long>(stat_stall_ns_ / 1000),
        static_cast<long long>(stat_forgiven_ns_ / 1000000),
        stat_idle_steps_, stat_idle_early_,
        static_cast<long long>(stat_idle_wait_ns_ / 1000000));
    stat_sleeps_ = 0;  stat_sleep_ns_ = 0;  stat_forgiven_ns_ = 0;
    stat_wake_over_ns_ = 0;  stat_stall_ns_ = 0;
    stat_idle_steps_ = 0;  stat_idle_early_ = 0;  stat_idle_wait_ns_ = 0;
    stat_guest_mark_ns_ = guest_ns;
    stat_wall_mark_ns_  = wall;
    stat_lag_published_ns_.store(wall - TargetWallNs(now), std::memory_order_relaxed);
}
#endif

void GuestCycleClock::OnCyclesRestored() {
    ref_cycle_      = Cycles();
    ref_wall_ns_    = wall_->NowNs();
    guest_ns_cycle_ = ref_cycle_;
    guest_ns_       = CyclesToNs(ref_cycle_);
#if CERF_DEV_MODE
    stat_guest_mark_ns_ = guest_ns_;
    stat_wall_mark_ns_  = ref_wall_ns_;
#endif
    Arm(throttle_, ref_cycle_ + NsToCycles(kThrottleSliceNs));
}

}

void CERFENGINE_FASTCALL cerfengine::host::ClockDispatch(ClockHost* clock) {
    static_cast<cerfcore::GuestCycleClock*>(clock)->DispatchDue();
}
