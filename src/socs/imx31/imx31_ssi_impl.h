#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../execution/guest_cycle_clock.h"
#include "imx31_avic.h"
#include "imx31_ccm.h"
#include "imx31_id.h"
#include "../freescale_module_clocks.h"
#include "../freescale_ssi_core.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace cerf_imx31_ssi_detail {

constexpr uint32_t kSize = 0x00004000u;

constexpr uint32_t SsiIndex(uint32_t base) { return base == 0x43FA0000u ? 1u : 2u; }

/* MCIMX31RM Table 2-3 (page 2-7): interrupt 11 "SSI2 module", 12 "SSI1 module". */
constexpr uint32_t SsiAvicSource(uint32_t base) { return base == 0x43FA0000u ? 12u : 11u; }

/* MCIMX31RM §45.3.3.2 (page 45-27): "The SSI transmit FIFO registers are 8x24-bit registers";
   Table 45-10 RFF0 (page 45-38): "When receive FIFO 0 contains 8 words". */
constexpr uint32_t kFifoDepth = 8u;

constexpr FreescaleSsiCoreConfig kConfig{kFifoDepth, 0u,
                                         FreescaleSsiStatusRules{false, false, true},
                                         false, false, false, false};

template <uint32_t kBase>
class Imx31SsiImpl : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }
    void OnReady() override {
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        clocked_       = module_clocks_->Running(Module());
        core_.Attach(
            [this] {
                return GuestCycleClock::Rate{emu_.Get<Imx31Ccm>().SsiClockHz(SsiIndex(kBase)), 1u};
            },
            [this] { return clocked_; },
            [this](bool on) {
                auto& avic = emu_.Get<Imx31Avic>();
                if (on) avic.AssertSource(SsiAvicSource(kBase));
                else    avic.DeassertSource(SsiAvicSource(kBase));
            });
        /* MCIMX31RM §3.6.3 / §3.6.5: a global or watchdog reset resets all peripherals. */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { core_.Reset(); });
        auto& clock = emu_.Get<GuestCycleClock>();
        clock.RegisterRateListener([this] { core_.OnCpuRate(); });
        clock.RegisterIdleListener([this] { OnModuleClock(); });
        clock.RegisterIdleExitListener([this] { OnModuleClock(); });
        module_clocks_->RegisterGateListener([this] { OnModuleClock(); });
        emu_.Get<Imx31Ccm>().RegisterRateListener([this] { core_.OnClockChange(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        uint32_t value = 0;
        if (core_.Read(addr - kBase, value)) return value;
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (core_.Write(addr - kBase, value)) return;
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    FreescaleSsiCore& Core() { return core_; }

    void SaveState(StateWriter& w) override { core_.Save(w); }
    void RestoreState(StateReader& r) override { core_.Restore(r); }
    void PostRestore() override {
        clocked_ = module_clocks_->Running(Module());
        core_.PostRestore();
    }

private:
    static constexpr FreescaleModule Module() {
        return SsiIndex(kBase) == 1u ? FreescaleModule::kSsi1 : FreescaleModule::kSsi2;
    }

    void OnModuleClock() {
        const bool on = module_clocks_->Running(Module());
        if (on == clocked_) return;
        clocked_ = on;
        core_.OnClockChange();
    }

    FreescaleSsiCore       core_{emu_, SsiIndex(kBase), kConfig};
    FreescaleModuleClocks* module_clocks_ = nullptr;
    bool                   clocked_       = true;
};

}

}
