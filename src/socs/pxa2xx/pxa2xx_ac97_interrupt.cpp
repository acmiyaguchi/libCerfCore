#include "pxa2xx_ac97_interrupt.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../irq_controller.h"
#include "../pxa255/pxa255_id.h"
#include "../pxa27x/pxa270_id.h"

namespace cerfcore {

REGISTER_SERVICE(Pxa2xxAc97Interrupt);

namespace {

/* Intel PXA255 Developer's Manual Table 4-35 (page 4-26): IS14 "AC97 Interrupt Pending"; Intel PXA27x
   Developer's Manual Table 25-3 (page 25-7): bit 14 AC97. */
constexpr int kIntcAc97 = 14;

}  // namespace

bool Pxa2xxAc97Interrupt::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Pxa255 || bd->GetSocId() == SocId::Pxa270);
}

void Pxa2xxAc97Interrupt::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    intc_  = &emu_.Get<IrqController>();
    ev_    = clock_->Add([this] { Refresh(); });
    emu_.Get<Pxa2xxAc97Link>().AddListener(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetLine(); });
}

void Pxa2xxAc97Interrupt::Attach(std::function<bool(uint64_t)> level, std::function<uint64_t(uint64_t)> next) {
    level_ = std::move(level);
    next_  = std::move(next);
}

void Pxa2xxAc97Interrupt::Refresh() {
    const uint64_t now = clock_->Cycles();
    Drive(level_(now));
    const uint64_t at = next_(now);
    if (at == kNever) {
        clock_->Disarm(ev_);
    } else {
        clock_->Arm(ev_, at);
    }
}

void Pxa2xxAc97Interrupt::Kick() { clock_->Arm(ev_, clock_->Cycles()); }

void Pxa2xxAc97Interrupt::Drive(bool level) {
    if (level == line_) return;
    line_ = level;
    if (level) {
        intc_->AssertIrq(kIntcAc97);
    } else {
        intc_->DeAssertIrq(kIntcAc97);
    }
}

void Pxa2xxAc97Interrupt::ResetLine() {
    Drive(false);
    clock_->Disarm(ev_);
}

void Pxa2xxAc97Interrupt::PostRestore() {
    line_ = false;
    intc_->DeAssertIrq(kIntcAc97);
    Refresh();
}

}
