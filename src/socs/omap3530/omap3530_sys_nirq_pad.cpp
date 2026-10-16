#include "omap3530_sys_nirq_pad.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../irq_controller.h"
#include "omap3530_gpio_bus.h"
#include "omap3530_id.h"
#include "omap3530_sysc_padconf.h"

namespace cerfcore {

namespace {

/* OMAP35x TRM SPRUF98Y Table 7-4 (page 779): CONTROL_PADCONF_SYS_NIRQ[15:0] at 0x4800 21E0, mode 0 sys_nirq, mode 4
   gpio_0; Figure 10-1 (page 1048): sys_nirq is M_IRQ_7; page 1049: the external interrupt is active at low level. */
constexpr uint32_t kPadSysNirq   = 0x1E0u;
constexpr uint32_t kModeSysNirq  = 0u;
constexpr uint32_t kModeGpio     = 4u;
constexpr int      kIrqSysNirq   = 7;
constexpr uint32_t kGpioSysNirq  = 0u;

}

bool Omap3530SysNirqPad::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Omap3530;
}

void Omap3530SysNirqPad::OnReady() {
    emu_.Get<Omap3530SyscPadconf>().RegisterPadListener(kPadSysNirq, [this] { Route(); });
}

void Omap3530SysNirqPad::Drive(bool low) {
    low_ = low;
    Route();
}

void Omap3530SysNirqPad::Route() {
    const uint32_t mode = emu_.Get<Omap3530SyscPadconf>().PadMuxMode(kPadSysNirq);
    auto&          intc = emu_.Get<IrqController>();
    if (mode == kModeSysNirq && low_) intc.AssertIrq(kIrqSysNirq);
    else                              intc.DeAssertIrq(kIrqSysNirq);
    if (mode == kModeGpio) emu_.Get<Omap3530GpioBus>().SetInputPin(kGpioSysNirq, !low_);
}

REGISTER_SERVICE(Omap3530SysNirqPad);

}
