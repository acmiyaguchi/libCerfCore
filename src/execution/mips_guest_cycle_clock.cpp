#include "engine_cycle_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../cpu/mips/mips_core_clock.h"
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include "engine_bridge.h"

namespace cerfcore {

namespace {

class MipsGuestCycleClock : public EngineCycleClock<cerfengine::MipsCpuState> {
public:
    using EngineCycleClock::EngineCycleClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kMips;
    }

    void OnReady() override {
        state_ = emu_.Get<EngineBridge>().Engine().Get<cerfengine::MipsCpu>().State();
        rate_  = emu_.Get<MipsCoreClock>().ResetRate();
        GuestCycleClock::OnReady();
    }

protected:
    Rate InitialRate() override { return rate_; }

private:
    Rate rate_;
};

}

REGISTER_SERVICE_AS(MipsGuestCycleClock, GuestCycleClock);

}
