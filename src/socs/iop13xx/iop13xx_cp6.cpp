#include "iop13xx_cp6.h"
#include "iop13xx_cp6_registers.h"
#include "iop13xx_timers.h"

#include <cerfcore/boards/board_context.h>
#include "iop13xx_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/arm/arm_jit.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <iterator>

namespace cerfcore {

bool Iop13xxCp6::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Iop13xx;
}

void Iop13xxCp6::OnReady() {
    jit_    = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>();
    timers_ = &emu_.Get<Iop13xxTimers>();
    {
        std::lock_guard<std::mutex> guard(state_mutex_);
        ResetStateLocked();
    }
    emu_.Get<GuestCpuReset>().SetCauseLatch(this);

    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        std::lock_guard<std::mutex> guard(state_mutex_);
        ResetStateLocked();
        NotifyLocked();
    });
}

bool Iop13xxCp6::HasPendingIrqLocked() const {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        if ((pending_[bank] & intctl_[bank] & ~intstr_[bank]) != 0) {
            return true;
        }
    }
    return false;
}

bool Iop13xxCp6::HasPendingFiqLocked() const {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        if ((pending_[bank] & intctl_[bank] & intstr_[bank]) != 0) {
            return true;
        }
    }
    return false;
}

void Iop13xxCp6::ResetStateLocked() {
    std::fill(std::begin(intctl_), std::end(intctl_), 0u);
    std::fill(std::begin(intstr_), std::end(intstr_), 0u);
    std::fill(std::begin(pending_), std::end(pending_), 0u);
    std::fill(std::begin(shared_irq_levels_), std::end(shared_irq_levels_), 0u);
    intbase_ = 0;
    intsize_ = 0;
}

void Iop13xxCp6::NotifyLocked() {
    if (HasPendingFiqLocked()) {
        emu_.Get<Fatal>().Die("IOP13xx CP6: FIQ source pending");
    }
    if (HasPendingIrqLocked())
        jit_->SetInterruptPending();
    else
        jit_->ClearInterruptPending();
}

void Iop13xxCp6::AssertIrq(int source_bit) {
    if (source_bit < 0 || source_bit >= 128) {
        emu_.Get<Fatal>().Die("Iop13xxCp6::AssertIrq: source %d outside 0..127", source_bit);
    }
    std::lock_guard<std::mutex> guard(state_mutex_);
    const uint32_t bank = static_cast<uint32_t>(source_bit) / 32u;
    const uint32_t bit = 1u << (static_cast<uint32_t>(source_bit) & 31u);
    pending_[bank] |= bit;
    NotifyLocked();
}

void Iop13xxCp6::AssertSubIrq(int main_source_bit, int sub_source_bit) {
    emu_.Get<Fatal>().Die("Iop13xxCp6::AssertSubIrq: unsupported sub-source main=%d sub=%d", main_source_bit,
                          sub_source_bit);
}

void Iop13xxCp6::DeAssertIrq(int source_bit) {
    if (source_bit < 0 || source_bit >= 128) return;
    std::lock_guard<std::mutex> guard(state_mutex_);
    const uint32_t bank = static_cast<uint32_t>(source_bit) / 32u;
    const uint32_t bit = 1u << (static_cast<uint32_t>(source_bit) & 31u);
    pending_[bank] &= ~bit;
    NotifyLocked();
}

void Iop13xxCp6::SetSharedIrqLevel(int source_bit, uint32_t contributor_bit, bool asserted) {
    if (source_bit < 0 || source_bit >= 128 || contributor_bit == 0u || !std::has_single_bit(contributor_bit)) {
        emu_.Get<Fatal>().Die("Iop13xxCp6::SetSharedIrqLevel: invalid source %d contributor 0x%08X",
                              source_bit, contributor_bit);
    }
    std::lock_guard<std::mutex> guard(state_mutex_);
    uint32_t& levels = shared_irq_levels_[source_bit];
    if (asserted)
        levels |= contributor_bit;
    else
        levels &= ~contributor_bit;
    const uint32_t bank = static_cast<uint32_t>(source_bit) / 32u;
    const uint32_t bit = 1u << (static_cast<uint32_t>(source_bit) & 31u);
    if (levels != 0u)
        pending_[bank] |= bit;
    else
        pending_[bank] &= ~bit;
    NotifyLocked();
}

void Iop13xxCp6::LatchWarmReset() {
    std::lock_guard<std::mutex> guard(state_mutex_);
    /* Intel 81341/81342 Developer's Manual, section 10.4.2, Table 448. */
    rcsr_ |= kResetCauseCoreTargeted;
}
void Iop13xxCp6::LatchColdReset() {
    std::lock_guard<std::mutex> guard(state_mutex_);
    /* Intel 81341/81342 Developer's Manual, section 10.4.2, Table 448. */
    rcsr_ = 0;
}
void Iop13xxCp6::LatchWatchdogReset() {
    std::lock_guard<std::mutex> guard(state_mutex_);
    /* Intel 81341/81342 Developer's Manual, section 10.4.2, Table 448. */
    rcsr_ |= kResetCauseWatchdog;
}

uint32_t Iop13xxCp6::InterruptVectorLocked() const {
    for (uint32_t bank = 0; bank < 4; ++bank) {
        const uint32_t active = pending_[bank] & intctl_[bank] & ~intstr_[bank];
        if (active == 0) continue;
        const uint32_t source = bank * 32u + std::countr_zero(active);
        const uint32_t shift = intsize_ + 1u;
        const uint32_t vector = intbase_ + (source << shift);
        return vector;
    }
    return 0xFFFFFFFFu;
}

uint32_t Iop13xxCp6::ReadRegisterLocked(uint32_t key) {
    switch (key) {
    case kCp6ResetCause: return rcsr_;
    case kCp6IntBase: return intbase_;
    case kCp6IntSize: return intsize_;
    case kCp6IntVec: return InterruptVectorLocked();
    case kCp6IntCtl0: return intctl_[0];
    case kCp6IntCtl1: return intctl_[1];
    case kCp6IntCtl2: return intctl_[2];
    case kCp6IntCtl3: return intctl_[3];
    default: emu_.Get<Fatal>().Die("IOP13xx CP6 unsupported read key 0x%04X", key);
    }
}

void Iop13xxCp6::WriteRegisterLocked(uint32_t key, uint32_t value) {
    switch (key) {
    case kCp6IntBase: intbase_ = value; break;
    case kCp6IntSize: intsize_ = value & 3u; break;
    case kCp6IntCtl0: intctl_[0] = value; break;
    case kCp6IntCtl1: intctl_[1] = value; break;
    case kCp6IntCtl2: intctl_[2] = value; break;
    case kCp6IntCtl3: intctl_[3] = value; break;
    case kCp6IntStr0: intstr_[0] = value; break;
    case kCp6IntStr1: intstr_[1] = value; break;
    case kCp6IntStr2: intstr_[2] = value; break;
    case kCp6IntStr3: intstr_[3] = value; break;
    default: emu_.Get<Fatal>().Die("IOP13xx CP6 unsupported write key 0x%04X value 0x%08X", key, value);
    }
    NotifyLocked();
}

uint32_t CERFENGINE_FASTCALL Iop13xxCp6::ReadHelper(void* context, uint32_t key) {
    Iop13xxCp6* self = static_cast<Iop13xxCp6*>(context);
    if (Iop13xxTimers::IsTimerKey(key)) return self->timers_->Read(key);
    std::lock_guard<std::mutex> guard(self->state_mutex_);
    return self->ReadRegisterLocked(key);
}

void CERFENGINE_FASTCALL Iop13xxCp6::WriteHelper(void* context, uint32_t key, uint32_t value) {
    Iop13xxCp6* self = static_cast<Iop13xxCp6*>(context);
    if (Iop13xxTimers::IsTimerKey(key)) {
        self->timers_->Write(key, value);
        return;
    }
    std::lock_guard<std::mutex> guard(self->state_mutex_);
    self->WriteRegisterLocked(key, value);
}

cerfengine::ArmCoprocDecision Iop13xxCp6::Decide(const cerfengine::ArmCoprocAccess& a) {
    using A = cerfengine::ArmCoprocAction;
    if (a.opc1 != 0u || a.opc2 != 0u || a.rt == 15u) return {.action = A::kUnimplemented};
    const uint32_t key = Iop13xxCp6Key(a.crn, a.crm, a.opc2, a.opc1);
    if (a.read) {
        return {.action = A::kReadCallbackKeyed, .context = this, .key = key,
                .read_keyed = &Iop13xxCp6::ReadHelper};
    }
    return {.action = A::kWriteCallbackKeyed, .context = this, .key = key,
            .write_keyed = &Iop13xxCp6::WriteHelper};
}

void Iop13xxCp6::SaveState(StateWriter& w) {
    {
        std::lock_guard<std::mutex> guard(state_mutex_);
        w.WriteBytes("intctl", intctl_, sizeof(intctl_));
        w.WriteBytes("intstr", intstr_, sizeof(intstr_));
        w.WriteBytes("pending", pending_, sizeof(pending_));
        w.WriteBytes("shared_irq_levels", shared_irq_levels_, sizeof(shared_irq_levels_));
        w.Write("intbase", intbase_);
        w.Write("intsize", intsize_);
        w.Write("rcsr", rcsr_);
    }
    timers_->SaveState(w);
}

void Iop13xxCp6::RestoreState(StateReader& r) {
    {
        std::lock_guard<std::mutex> guard(state_mutex_);
        r.ReadBytes("intctl", intctl_, sizeof(intctl_));
        r.ReadBytes("intstr", intstr_, sizeof(intstr_));
        r.ReadBytes("pending", pending_, sizeof(pending_));
        r.ReadBytes("shared_irq_levels", shared_irq_levels_, sizeof(shared_irq_levels_));
        r.Read("intbase", intbase_);
        r.Read("intsize", intsize_);
        r.Read("rcsr", rcsr_);
    }
    timers_->RestoreState(r);
}

void Iop13xxCp6::PostRestoreState() {
    {
        std::lock_guard<std::mutex> guard(state_mutex_);
        NotifyLocked();
    }
    timers_->PostRestore();
}

REGISTER_SERVICE_AS(Iop13xxCp6, IrqController);

}
