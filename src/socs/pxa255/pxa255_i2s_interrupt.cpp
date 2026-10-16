#include "pxa255_i2s_interrupt.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "../irq_controller.h"
#include "pxa255_id.h"

namespace cerfcore {

REGISTER_SERVICE(Pxa255I2sInterrupt);

namespace {

/* Intel PXA255 Developer's Manual Table 4-35 (page 4-26): IS13 "I2S Interrupt Pending". */
constexpr int kIntcI2s = 13;
/* Intel PXA255 Developer's Manual Table 14-10 (page 14-14) SAIMR: ROR 6, TUR 5, RFS 4, TFS 3. */
constexpr uint32_t kSaimrDefined = 0x78u;

}

bool Pxa255I2sInterrupt::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

void Pxa255I2sInterrupt::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    intc_  = &emu_.Get<IrqController>();
    ev_    = clock_->Add([this] { Refresh(); });
}

void Pxa255I2sInterrupt::Attach(std::function<bool()> enabled, std::function<uint32_t(uint64_t)> live,
                                std::function<uint64_t(uint64_t, uint32_t, uint32_t)> next) {
    enabled_ = std::move(enabled);
    live_    = std::move(live);
    next_    = std::move(next);
}

void Pxa255I2sInterrupt::WriteSaimr(uint32_t value) {
    saimr_ = value & kSaimrDefined;
    Refresh();
}

/* Intel PXA255 Developer's Manual Table 14-9 (page 14-13) SAICR: ROR "Clear Receive FIFO overrun Interrupt
   and ROR status bit in SASR0", TUR "Clear Transmit FIFO under-run Interrupt and TUR status bit in SASR0". */
void Pxa255I2sInterrupt::WriteSaicr(uint64_t now, uint32_t value) {
    live_(now);
    if ((value & kTur) != 0u) tur_ = false;
    if ((value & kRor) != 0u) ror_ = false;
    Refresh();
}

/* Intel PXA255 Developer's Manual Table 14-7 (page 14-12) ROR: "Can interrupt processor if bit6 of Serial
   Audio Interrupt Mask Register is set"; TUR, RFS and TFS the same with bits 5, 4 and 3. Section 14.6.1
   (page 14-8): clearing ENB "disables all interrupts". */
void Pxa255I2sInterrupt::Refresh() {
    if (!enabled_()) {
        Drive(false);
        clock_->Disarm(ev_);
        return;
    }
    const uint64_t now    = clock_->Cycles();
    const uint32_t live   = live_(now);
    const uint32_t status = live | Sticky();
    Drive((status & saimr_) != 0u);
    const uint64_t at = saimr_ == 0u ? kNever : next_(now, saimr_ & ~status, saimr_ & status);
    if (at == kNever) {
        clock_->Disarm(ev_);
    } else {
        clock_->Arm(ev_, at);
    }
}

void Pxa255I2sInterrupt::Drive(bool level) {
    if (level == line_) return;
    line_ = level;
    if (level) {
        intc_->AssertIrq(kIntcI2s);
    } else {
        intc_->DeAssertIrq(kIntcI2s);
    }
}

void Pxa255I2sInterrupt::ResetRegisters() {
    saimr_ = 0u;
    tur_   = false;
    ror_   = false;
    Drive(false);
    clock_->Disarm(ev_);
}

void Pxa255I2sInterrupt::Save(StateWriter& w) {
    w.Write("saimr", saimr_);
    w.Write<uint8_t>("i2s_tur", tur_ ? 1u : 0u);
    w.Write<uint8_t>("i2s_ror", ror_ ? 1u : 0u);
}

void Pxa255I2sInterrupt::Restore(StateReader& r) {
    uint8_t tur = 0, ror = 0;
    r.Read("saimr", saimr_);
    r.Read("i2s_tur", tur);
    r.Read("i2s_ror", ror);
    tur_ = tur != 0u;
    ror_ = ror != 0u;
    clock_->Disarm(ev_);
}

void Pxa255I2sInterrupt::PostRestore() {
    line_ = false;
    intc_->DeAssertIrq(kIntcI2s);
    Refresh();
}

}
