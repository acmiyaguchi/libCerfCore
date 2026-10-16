#include "omap3530_mcbsp_clock_inputs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "omap3530_cm_clock_control.h"
#include "omap3530_cm_core.h"
#include "omap3530_cm_per.h"
#include "omap3530_id.h"
#include "omap3530_mcbsp_wiring.h"
#include "omap3530_sysc_padconf.h"

namespace cerfcore {

bool Omap3530McBspClockInputs::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Omap3530;
}

void Omap3530McBspClockInputs::OnReady() {
    clock_control_ = &emu_.Get<Omap3530CmClockControl>();
    core_          = &emu_.Get<Omap3530CmCore>();
    per_           = &emu_.Get<Omap3530CmPer>();
    padconf_       = &emu_.Get<Omap3530SyscPadconf>();
    pad_           = emu_.Get<Omap3530McBspWiring>().ClksPad();
    core_->RegisterClockListener([this] { Notify(); });
    per_->RegisterClockListener([this] { Notify(); });
    padconf_->RegisterDevconfListener([this] { Notify(); });
    if (pad_ != nullptr) pad_->RegisterPadListener([this] { Notify(); });
}

const Omap3530CmMcBspGateBlock& Omap3530McBspClockInputs::GateBlock(uint32_t port) const {
    if (port == 1u || port == 5u) return *core_;
    return *per_;
}

bool Omap3530McBspClockInputs::FclkEnabled(uint32_t port) const {
    return GateBlock(port).McBspFclkEnabled(port);
}

GuestCycleClock::Rate Omap3530McBspClockInputs::Iclk(uint32_t port) const {
    return GateBlock(port).McBspIclkEnabled(port) ? core_->L4Iclk() : GuestCycleClock::Rate{0u, 1u};
}

/* OMAP35x TRM SPRUF98Y Table 7-80 / Table 7-96: a CLKS select bit 0 takes the PRCM functional clock,
   1 the external pin McBSP_CLKS. */
GuestCycleClock::Rate Omap3530McBspClockInputs::Clks(uint32_t port) const {
    if (!padconf_->McBspClksFromPad(port)) {
        return FclkEnabled(port) ? clock_control_->Fclk96M() : GuestCycleClock::Rate{0u, 1u};
    }
    return pad_ != nullptr ? pad_->PadRate() : GuestCycleClock::Rate{0u, 1u};
}

/* SPRUF98Y Table 7-80 MCBSP2_CLKS note: "CLKR input is from the pin McBSP2_CLKX and FSR input is from
   the pin McBSP2_FSX"; MCBSP1_CLKR [3] and MCBSP1_FSR [4] select the McBSP1 sources. */
bool Omap3530McBspClockInputs::ClkrFromClkx(uint32_t port) const {
    return port != 1u || padconf_->McBsp1ClkrFromClkx();
}

bool Omap3530McBspClockInputs::FsrFromFsx(uint32_t port) const {
    return port != 1u || padconf_->McBsp1FsrFromFsx();
}

void Omap3530McBspClockInputs::RegisterListener(std::function<void()> fn) {
    listeners_.push_back(std::move(fn));
}

void Omap3530McBspClockInputs::Notify() {
    for (auto& fn : listeners_) fn();
}

REGISTER_SERVICE(Omap3530McBspClockInputs);

}
