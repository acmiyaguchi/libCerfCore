#include "engine_cycle_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../cpu/arm/arm_core_clock.h"
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/cpu_state.h>
#include "engine_bridge.h"

#if CERF_DEV_MODE
#include <atomic>

#include <cerfcore/core/log.h>
#include "../core/rate_probe.h"
#endif

namespace {

class ArmGuestCycleClock : public EngineCycleClock<cerfengine::ArmCpuState> {
public:
    using EngineCycleClock::EngineCycleClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm;
    }

    void OnReady() override {
        state_      = emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmCpu>().State();
        reset_rate_ = emu_.Get<ArmCoreClock>().ResetRate();
        GuestCycleClock::OnReady();
#if CERF_DEV_MODE
        emu_.Get<RateProbe>().RegisterStallDump([this] {
            const auto ld = [](uint32_t& word) {
                return std::atomic_ref<uint32_t>(word).load(std::memory_order_acquire);
            };
            const uint32_t counter  = ld(state_->guest_cycle_counter);
            const uint32_t folded   = ld(state_->guest_cycle_folded);
            const uint32_t hi32     = ld(state_->guest_cycle_hi);
            const uint32_t deadline = ld(state_->guest_cycle_deadline);
            const uint64_t hi       = hi32 + (counter < folded ? 1u : 0u);
            LOG(Jit, "[CYCLECLK] stall pc=0x%08X counter=%u deadline=%u (%+d) "
                     "hi=%u folded=%u cycles=%llu chain_exit=0x%X irq_pend=%u "
                     "deep_sleep=%u lag_ms_at_last_guest_s=%lld rate=%llu/%llu Hz\n",
                ld(state_->gprs[cerfengine::ArmGpr::kR15]), counter, deadline,
                static_cast<int32_t>(counter - deadline), hi32, folded,
                static_cast<unsigned long long>((hi << 32) | counter),
                ld(state_->chain_exit_request), ld(state_->irq_interrupt_pending),
                ld(state_->deep_sleep),
                static_cast<long long>(LagNsAtLastSecond() / 1000000),
                static_cast<unsigned long long>(ClockRate().num),
                static_cast<unsigned long long>(ClockRate().den));
        });
#endif
    }

protected:
    Rate InitialRate() override { return reset_rate_; }

private:
    Rate reset_rate_ = {};
};

}

REGISTER_SERVICE_AS(ArmGuestCycleClock, GuestCycleClock);
