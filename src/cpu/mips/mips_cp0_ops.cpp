#include "mips_cp0_ops.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/mips_engine_bridge.h"
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_mmu.h>
#include "../mips_processor_config.h"
#include "mips_core_clock.h"
#include "mips_tlb_model.h"

REGISTER_SERVICE(MipsCp0Ops);

namespace {

/* EntryHi (CP0 r10): VPN<31:12> PID<11:6>, reserved <5:0> "ignores writes,
   returns zero when read" (TMPR3911.pdf Fig 3.3.6). */
constexpr uint32_t kTx39EntryHiVpnMask = 0xFFFFF000u;
constexpr uint32_t kTx39EntryHiPidMask = 0x00000FC0u;

}

bool MipsCp0Ops::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kMips;
}

void MipsCp0Ops::OnReady() {
    MipsEngineBridge&   bridge = emu_.Get<MipsEngineBridge>();
    cerfengine::Engine& engine = bridge.Engine();
    cpu_state_   = engine.Get<cerfengine::MipsCpu>().State();
    tlb_context_ = &engine.Get<cerfengine::MipsMmu>().TlbContext();
    tlb_         = &emu_.Get<MipsTlbModel>();
    config_      = &emu_.Get<MipsProcessorConfig>();
    if (!config_->HasCounter()) return;
    clock_         = &emu_.Get<GuestCycleClock>();
    core_clock_    = &emu_.Get<MipsCoreClock>();
    compare_event_ = clock_->Add([this] { OnCompareMatch(); });
    core_clock_->RegisterCountRateListener([this] { AnchorSavedCount(); });
    bridge.RegisterPostResetListener([this] { OnCpuReset(); });
    OnCpuReset();
}

void MipsCp0Ops::SetCountRatio() {
    const uint64_t cycles = core_clock_->CyclesPerCountTick();
    if (!count_.SetRatio(cycles, 1u)) {
        emu_.Get<Fatal>().Die("MipsCp0Ops: %llu cycles per Count tick overflows the counter",
                              static_cast<unsigned long long>(cycles));
    }
}

void MipsCp0Ops::ArmCompare(uint64_t now) {
    clock_->Arm(compare_event_, count_.NextMatchCycle(cpu_state_->cp0_compare, now));
}

/* IP7 "is set automatically whenever the value of the Count register equals the value of the
   Compare register" (VR4121 UM §10.4); QEMU cp0_timer cpu_mips_timer_expire. */
void MipsCp0Ops::OnCompareMatch() {
    cpu_state_->cp0_cause |= cerfengine::kMipsCauseTimerIp;
    ArmCompare(clock_->Cycles());
}

void MipsCp0Ops::OnCpuReset() {
    if (!config_->HasCounter()) return;
    const uint64_t now = clock_->Cycles();
    SetCountRatio();
    count_.Anchor(now, 0u);
    ArmCompare(now);
}

void MipsCp0Ops::SaveCount() {
    cerfengine::MipsCpuState& s = *cpu_state_;
    if (!config_->HasCounter()) {
        s.count_save           = 0u;
        s.count_save_phase     = 0u;
        s.count_save_phase_den = 0u;
        return;
    }
    const uint64_t now = clock_->Cycles();
    const uint64_t den = count_.PhaseDenominator();
    if (den > UINT32_MAX) {
        emu_.Get<Fatal>().Die("MipsCp0Ops: Count phase denominator %llu does not fit the save",
                              static_cast<unsigned long long>(den));
    }
    s.count_save           = count_.CountAt(now);
    s.count_save_phase     = count_.PhaseAt(now);
    s.count_save_phase_den = static_cast<uint32_t>(den);
}

void MipsCp0Ops::OnCpuStateRestored() {
    if (!config_->HasCounter()) return;
    AnchorSavedCount();
}

void MipsCp0Ops::AnchorSavedCount() {
    const cerfengine::MipsCpuState& s = *cpu_state_;
    const uint64_t now = clock_->Cycles();
    SetCountRatio();
    count_.AnchorAtPhase(now, s.count_save, s.count_save_phase, s.count_save_phase_den);
    ArmCompare(now);
}

uint32_t MipsCp0Ops::CountNow() {
    return count_.CountAt(clock_->Cycles());
}

uint32_t CERFENGINE_FASTCALL MipsCp0Ops::ReadRandom(void* ops) {
    MipsCp0Ops* self = static_cast<MipsCp0Ops*>(ops);
    return self->tlb_->RandomIndex(self->cpu_state_);
}

uint32_t CERFENGINE_FASTCALL MipsCp0Ops::ReadCount(void* ops) {
    return static_cast<MipsCp0Ops*>(ops)->CountNow();
}

void CERFENGINE_FASTCALL MipsCp0Ops::WriteCount(void* ops, uint32_t value) {
    MipsCp0Ops* self = static_cast<MipsCp0Ops*>(ops);
    const uint64_t now = self->clock_->Cycles();
    self->count_.SetCountAt(now, value);
    self->ArmCompare(now);
}

void CERFENGINE_FASTCALL MipsCp0Ops::WriteCompare(void* ops, uint32_t value) {
    MipsCp0Ops* self = static_cast<MipsCp0Ops*>(ops);
    cerfengine::MipsCpuState& s = *self->cpu_state_;
    s.cp0_compare = value;
    s.cp0_cause  &= ~cerfengine::kMipsCauseTimerIp;
    self->ArmCompare(self->clock_->Cycles());
}

void CERFENGINE_FASTCALL MipsCp0Ops::WriteEntryHiR3000(void* ops, uint32_t value) {
    MipsCp0Ops* self = static_cast<MipsCp0Ops*>(ops);
    cerfengine::MipsCpuState& s = *self->cpu_state_;
    const uint32_t old = s.cp0_entryhi;
    const uint32_t val = value & (kTx39EntryHiVpnMask | kTx39EntryHiPidMask);
    s.cp0_entryhi = val;
    if ((old & kTx39EntryHiPidMask) != (val & kTx39EntryHiPidMask)) {
        self->tlb_context_->InvalidateAddressSpace();
    }
}

void CERFENGINE_FASTCALL MipsCp0Ops::WriteEntryHiR4000(void* ops, uint32_t value) {
    MipsCp0Ops* self = static_cast<MipsCp0Ops*>(ops);
    /* QEMU helper_mtc0_entryhi (MIPS cp0_helper.c): write VPN2+ASID, preserve
       the reserved field, flush on an ASID change. mask = VPN2(VA[31:S+1]) | ASID. */
    cerfengine::MipsCpuState& s = *self->cpu_state_;
    const uint32_t kMask = cerfengine::MipsVpn2Mask(s.min_page_shift) | 0xFFu;
    const uint32_t old = s.cp0_entryhi;
    const uint32_t val = (value & kMask) | (old & ~kMask);
    s.cp0_entryhi = val;
    if ((old & 0xFFu) != (val & 0xFFu)) {
        self->tlb_context_->InvalidateAddressSpace();
    }
}
