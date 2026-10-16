#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "imx51_ccm.h"
#include "imx51_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "../freescale_module_clocks.h"
#include "../freescale_ssi_core.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../irq_controller.h"

#include <cstdint>

namespace cerf_imx51_ssi_detail {

constexpr uint32_t kSize = 0x00004000u;   /* AIPS slot, MCIMX51RM Table 2-1 */

constexpr uint32_t SsiIndex(uint32_t base) {
    return base == 0x83FCC000u ? 1u : base == 0x70014000u ? 2u : 3u;
}

/* MCIMX51RM Table 3-2: IRQ 29 SSI1 and 30 SSI2 (page 3-2), 96 SSI3 (page 3-4). */
constexpr int SsiIrq(uint32_t base) {
    return SsiIndex(base) == 1u ? 29 : SsiIndex(base) == 2u ? 30 : 96;
}

/* MCIMX51RM §56.3.3.2 (page 56-33): "The SSI Transmit FIFO registers are 15 x 32-bit registers";
   Table 56-13 RFF0 (page 56-45): "When Rx FIFO0 contains 15 words, the maximum it can hold". */
constexpr uint32_t kFifoDepth = 15u;

/* MCIMX51RM Table 56-12 TE (page 56-41): "SSI expects 4 setup clock cycles before arrival of
   frame-sync for frame-sync to be accepted by SSI." */
constexpr uint32_t kFrameSyncSetupClocks = 4u;

constexpr FreescaleSsiCoreConfig kConfig{kFifoDepth, kFrameSyncSetupClocks,
                                         FreescaleSsiStatusRules{true, true, false},
                                         true, true, true, true};

template <uint32_t kBase>
class Imx51SsiImpl : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx51;
    }
    void OnReady() override {
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        clocked_       = module_clocks_->Running(IpgModule());
        ssi_clk_on_    = module_clocks_->Running(Module());
        core_.Attach([this] {
                         return ssi_clk_on_ ? emu_.Get<Imx51Ccm>().SsiClockRate(SsiIndex(kBase))
                                            : GuestCycleClock::Rate{0u, 1u};
                     },
                     [this] { return clocked_; },
                     [this](bool on) {
                         auto& irq = emu_.Get<IrqController>();
                         if (on) irq.AssertIrq(SsiIrq(kBase));
                         else    irq.DeAssertIrq(SsiIrq(kBase));
                     });
        /* MCIMX51RM Table 54-10: system_rst_b "Resets functional modules" in every reset row. */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { core_.Reset(); });
        auto& clock = emu_.Get<GuestCycleClock>();
        clock.RegisterRateListener([this] { core_.OnCpuRate(); });
        clock.RegisterIdleListener([this] { OnModuleClock(); });
        clock.RegisterIdleExitListener([this] { OnModuleClock(); });
        module_clocks_->RegisterGateListener([this] { OnModuleClock(); });
        emu_.Get<Imx51Ccm>().RegisterRateListener([this] { core_.OnClockChange(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        uint32_t value = 0;
        core_.Read(addr - kBase, value);
        return value;
    }

    void WriteWord(uint32_t addr, uint32_t value) override { core_.Write(addr - kBase, value); }

    FreescaleSsiCore& Core() { return core_; }

    void SaveState(StateWriter& w) override { core_.Save(w); }
    void RestoreState(StateReader& r) override { core_.Restore(r); }
    void PostRestore() override {
        clocked_    = module_clocks_->Running(IpgModule());
        ssi_clk_on_ = module_clocks_->Running(Module());
        core_.PostRestore();
    }

private:
    static constexpr FreescaleModule Module() {
        return SsiIndex(kBase) == 1u   ? FreescaleModule::kSsi1
               : SsiIndex(kBase) == 2u ? FreescaleModule::kSsi2
                                       : FreescaleModule::kSsi3;
    }

    static constexpr FreescaleModule IpgModule() {
        return SsiIndex(kBase) == 1u   ? FreescaleModule::kSsi1Ipg
               : SsiIndex(kBase) == 2u ? FreescaleModule::kSsi2Ipg
                                       : FreescaleModule::kSsi3Ipg;
    }

    void OnModuleClock() {
        const bool ipg = module_clocks_->Running(IpgModule());
        const bool ssi = module_clocks_->Running(Module());
        if (ipg == clocked_ && ssi == ssi_clk_on_) return;
        clocked_    = ipg;
        ssi_clk_on_ = ssi;
        core_.OnClockChange();
    }

    FreescaleSsiCore        core_{emu_, SsiIndex(kBase), kConfig};
    FreescaleModuleClocks*  module_clocks_ = nullptr;
    bool                    clocked_       = true;
    bool                    ssi_clk_on_    = true;
};

}
