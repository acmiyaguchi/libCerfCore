#include "sa1111_sbi.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/state/state_stream.h>
#include "sa1111_reset_line.h"
#include "sa1111_system_controller.h"

bool Sa1111Sbi::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

/* SA-1111 Developer's Manual §2.3: "Unless indicated otherwise, all register bits are set
   to zero during reset." */
void Sa1111Sbi::OnChipReset(bool held) {
    if (!held) return;
    skcr_   = kSkcrReset;
    smcr_   = kSmcrReset;
    asleep_ = false;
}

/* SA-1111 Developer's Manual §2.4.3 (page 2-8) State 5: "This state is entered from sleep mode
   anytime nCS goes active (any read or write to SA-1111)." */
void Sa1111Sbi::WakeOnAccess() {
    if (!asleep_) return;
    asleep_ = false;
    emu_.Get<Sa1111SystemController>().NotifyClockListeners();
}

/* §2.4.3 (page 2-7): State 2 Shutdown_1 "causes the Power Control Register to be cleared, which
   disables all clocks"; State 3 Shutdown_2 "disables RCLK and the PLL". */
void Sa1111Sbi::EnterSleep() {
    asleep_ = true;
    skcr_   = (skcr_ & ~kSkcrRclkEn) | kSkcrVcoOff;
    emu_.Get<Sa1111SystemController>().OnSleepEntry();
}

/* SA-1111 Developer's Manual Table 1-2 (printed 1-4): MBGNT "Memory bus grant, from SA-1110
   processor." */
void Sa1111Sbi::SetHostPins(std::function<Mbgnt()> mbgnt) {
    mbgnt_ = std::move(mbgnt);
}

void Sa1111Sbi::OnHostPinsChange() {
    NotifyGrant();
}

void Sa1111Sbi::RegisterGrantListener(std::function<void()> fn) {
    grant_listeners_.push_back(std::move(fn));
}

void Sa1111Sbi::NotifyGrant() {
    for (auto& fn : grant_listeners_) fn();
}

/* Table 3-10 MBGE: "0=MBGNT is gated and disabled. 1=MBGNT is enabled."; Figure 3-7 (printed
   3-13): MBGNT high for the whole bus tenure. */
Sa1111Sbi::BusGrant Sa1111Sbi::Grant() const {
    if ((smcr_ & kSmcrMbge) == 0u) return BusGrant::Stalled;
    return mbgnt_() == Mbgnt::Low ? BusGrant::Stalled : BusGrant::Granted;
}

uint32_t Sa1111Sbi::UnitReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        case 0x00: return skcr_;
        case 0x04: return smcr_;
        case 0x08: return 0x690CC200u;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Sa1111Sbi::UnitWriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off != 0x00u && off != 0x04u) HaltUnsupportedAccess("WriteWord", addr, value);
    /* SA-1111 Developer's Manual §2.3 (page 2-4): "When nRESET is asserted, all on-chip activity
       halts". */
    if (emu_.Get<Sa1111ResetLine>().NresetAsserted()) return;
    auto& sysc = emu_.Get<Sa1111SystemController>();
    if (off == 0x00u) {
        skcr_ = value & kSkcrDefined & ~(kSkcrSleep | kSkcrDoze);
        emu_.Get<Sa1111ResetLine>().OnSkcrWrite(BusClocksEnabled());
        if ((value & kSkcrDoze) != 0u) sysc.OnDozeEntry();
        if ((value & kSkcrSleep) != 0u) EnterSleep();
        sysc.NotifyClockListeners();
        return;
    }
    smcr_ = value & kSmcrDefined;
    sysc.NotifyClockListeners();
    NotifyGrant();
}

void Sa1111Sbi::SaveState(StateWriter& w) {
    w.Write("skcr", skcr_);
    w.Write("smcr", smcr_);
    w.Write<uint8_t>("sbi_asleep", asleep_ ? 1u : 0u);
    emu_.Get<Sa1111ResetLine>().SaveState(w);
}

void Sa1111Sbi::RestoreState(StateReader& r) {
    r.Read("skcr", skcr_);
    r.Read("smcr", smcr_);
    uint8_t asleep = 0u;
    r.Read("sbi_asleep", asleep);
    asleep_ = asleep != 0u;
    emu_.Get<Sa1111ResetLine>().RestoreState(r);
}

REGISTER_SERVICE(Sa1111Sbi);
