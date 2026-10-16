#include "omap3530_sdma_irq.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "../irq_controller.h"

#include <cstring>

namespace cerfcore {

uint32_t Omap3530SdmaIrq::ReadIrqRegister(uint32_t off) const {
    if (off < kIrqEnableL0) return irqstatus_l_[(off - kIrqStatusL0) / 4u];
    return irqenable_l_[(off - kIrqEnableL0) / 4u];
}

void Omap3530SdmaIrq::WriteIrqRegister(uint32_t off, uint32_t value) {
    if (off < kIrqEnableL0) irqstatus_l_[(off - kIrqStatusL0) / 4u] &= ~value;
    else                    irqenable_l_[(off - kIrqEnableL0) / 4u] = value;
    UpdateIrqLines();
}

void Omap3530SdmaIrq::ResetIrqRegisters() {
    std::memset(irqstatus_l_, 0, sizeof(irqstatus_l_));
    std::memset(irqenable_l_, 0, sizeof(irqenable_l_));
}

void Omap3530SdmaIrq::UpdateIrqLines() {
    const uint32_t fired = FiredChannels();
    for (int j = 0; j < 4; ++j) irqstatus_l_[j] |= fired & irqenable_l_[j];
    auto& intc = emu_.Get<IrqController>();
    for (int j = 0; j < 4; ++j) {
        const int irq = IrqForLine(j);
        if (irq < 0) continue;
        const bool want_high = (irqstatus_l_[j] & irqenable_l_[j]) != 0u;
        if (want_high == irq_line_high_[j]) continue;
        irq_line_high_[j] = want_high;
        if (want_high) intc.AssertIrq(irq);
        else           intc.DeAssertIrq(irq);
    }
}

void Omap3530SdmaIrq::SaveIrqRegisters(StateWriter& w) {
    w.WriteBytes("irqstatus_l", irqstatus_l_, sizeof(irqstatus_l_));
    w.WriteBytes("irqenable_l", irqenable_l_, sizeof(irqenable_l_));
}

void Omap3530SdmaIrq::RestoreIrqRegisters(StateReader& r) {
    r.ReadBytes("irqstatus_l", irqstatus_l_, sizeof(irqstatus_l_));
    r.ReadBytes("irqenable_l", irqenable_l_, sizeof(irqenable_l_));
}

void Omap3530SdmaIrq::SaveIrqLines(StateWriter& w) {
    w.WriteBytes("irq_line_high", irq_line_high_, sizeof(irq_line_high_));
}

void Omap3530SdmaIrq::RestoreIrqLines(StateReader& r) {
    r.ReadBytes("irq_line_high", irq_line_high_, sizeof(irq_line_high_));
}

}
