#include "tps65950_interrupts.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ti_omap35x_evm/ti_omap35x_evm_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "tps65950_board.h"
#include "tps65950_reset_image.h"

namespace cerfcore {

namespace {

/* TPS65950 TRM SWCU050E Table 4-101 (page 197): PWR_ISR1 RTC_IT [3]; Table 4-109 (page 200): PWR_EDR1
   RTC_IT_RISING [7], RTC_IT_FALLING [6]; Table 4-113 (page 202): PWR_SIH_CTRL COR [2], PENDDIS [1], EXCLEN [0]. */
constexpr uint8_t kRtcIt         = 1u << 3;
constexpr uint8_t kEdrRtcRising  = 1u << 7;
constexpr uint8_t kEdrRtcFalling = 1u << 6;
constexpr uint8_t kCor           = 1u << 2;
constexpr uint8_t kPenddis       = 1u << 1;
constexpr uint8_t kExclen        = 1u << 0;
constexpr uint8_t kSihCtrlBits   = kCor | kPenddis | kExclen;

/* TPS65950 TRM SWCU050E Table 4-3 (page 159): PIH_ISR5 = PWR. */
constexpr uint8_t kPihPwr = 1u << 5;

}

bool Tps65950Interrupts::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
}

void Tps65950Interrupts::OnReady() {
    board_ = &emu_.Get<Tps65950Board>();
    LoadPowerOn();
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind kind) {
        if (kind != ResetLineKind::Rtc) return;
        LoadPowerOn();
        reset_ = true;
    });
    reset.RegisterResetReleaseListener([this] {
        if (!reset_) return;
        reset_ = false;
        Drive();
    });
}

void Tps65950Interrupts::LoadPowerOn() {
    isr_[0]     = tps65950::PowerRtcPowerOn(kPwrIsr1);
    isr_[1]     = tps65950::PowerRtcPowerOn(kPwrIsr2);
    imr_[0]     = tps65950::PowerRtcPowerOn(kPwrImr1);
    imr_[1]     = tps65950::PowerRtcPowerOn(kPwrImr2);
    edr_[0]     = tps65950::PowerRtcPowerOn(kPwrEdr1);
    edr_[1]     = tps65950::PowerRtcPowerOn(kPwrEdr2);
    sih_ctrl_   = tps65950::PowerRtcPowerOn(kPwrSihCtrl);
    pending_[0] = 0u;
    pending_[1] = 0u;
}

/* TPS65950 TRM SWCU050E page 157, section 4.4.2.3: with PENDDIS 0 a second event on one interrupt line before the
   status register is read is buffered and treated later; a third is lost. */
void Tps65950Interrupts::Latch(int line, uint8_t bit) {
    if ((isr_[line] & bit) == 0u) {
        isr_[line] |= bit;
        return;
    }
    if ((sih_ctrl_ & kPenddis) == 0u) pending_[line] |= bit;
}

void Tps65950Interrupts::Clear(int line, uint8_t bits) {
    isr_[line] &= static_cast<uint8_t>(~bits);
    const uint8_t again = pending_[line] & bits;
    pending_[line] &= static_cast<uint8_t>(~again);
    isr_[line] |= again;
}

/* TPS65950 TRM SWCU050E Table 4-2 (page 157): with EXCLEN 1 an event goes to ISR1 when IMR1 is unmasked, else to ISR2
   when IMR2 is unmasked; with EXCLEN 0 it goes to every unmasked line. */
void Tps65950Interrupts::Event(uint8_t bit) {
    const bool to1 = (imr_[0] & bit) == 0u;
    const bool to2 = (imr_[1] & bit) == 0u && ((sih_ctrl_ & kExclen) == 0u || !to1);
    if (to1) Latch(0, bit);
    if (to2) Latch(1, bit);
}

/* TPS65950 TRM SWCU050E page 160, section 4.5.2: a set ISR field asserts the SIH line to the PIH, and the PIH drives
   INT1 low; clearing the ISR releases both. */
void Tps65950Interrupts::Drive() {
    const bool int1 = isr_[0] != 0u;
    if (int1 == int1_) return;
    int1_ = int1;
    board_->DriveInt1(int1);
}

void Tps65950Interrupts::SetRtcIt(bool level) {
    if (level == rtc_it_) return;
    rtc_it_ = level;
    if ((edr_[0] & (level ? kEdrRtcRising : kEdrRtcFalling)) != 0u) Event(kRtcIt);
    Drive();
}

uint8_t Tps65950Interrupts::ReadPower(uint8_t sub) {
    switch (sub) {
        case kPwrIsr1:
        case kPwrIsr2: {
            const int     line  = sub == kPwrIsr2 ? 1 : 0;
            const uint8_t value = isr_[line];
            if ((sih_ctrl_ & kCor) != 0u) {
                Clear(line, 0xFFu);
                Drive();
            }
            return value;
        }
        case kPwrImr1:    return imr_[0];
        case kPwrImr2:    return imr_[1];
        case kPwrEdr1:    return edr_[0];
        case kPwrEdr2:    return edr_[1];
        case kPwrSihCtrl: return sih_ctrl_;
    }
    return 0u;
}

void Tps65950Interrupts::WritePower(uint8_t sub, uint8_t value) {
    switch (sub) {
        case kPwrIsr1:    Clear(0, value); break;
        case kPwrIsr2:    Clear(1, value); break;
        case kPwrImr1:    imr_[0] = value; break;
        case kPwrImr2:    imr_[1] = value; break;
        case kPwrEdr1:    edr_[0] = value; break;
        case kPwrEdr2:    edr_[1] = value; break;
        case kPwrSihCtrl: sih_ctrl_ = value & kSihCtrlBits; break;
        case kPwrSir:
            if (value != 0u) {
                emu_.Get<Fatal>().Die("Tps65950Interrupts: PWR_SIR write 0x%02X requests a test-mode software "
                                      "interrupt; not modelled", value);
            }
            break;
    }
    Drive();
}

uint8_t Tps65950Interrupts::ReadPih(uint8_t sub) const {
    switch (sub) {
        case kPihIsrP1: return isr_[0] != 0u ? kPihPwr : 0u;
        case kPihIsrP2: return isr_[1] != 0u ? kPihPwr : 0u;
    }
    return 0u;
}

void Tps65950Interrupts::WritePih(uint8_t sub, uint8_t value) {
    if (sub == kPihSir && value == 0u) return;
    emu_.Get<Fatal>().Die("Tps65950Interrupts: write 0x%02X to PIH register 0x%02X; not modelled", value, sub);
}

void Tps65950Interrupts::Save(StateWriter& w) {
    w.WriteBytes("tps_int_isr", isr_, sizeof(isr_));
    w.WriteBytes("tps_int_imr", imr_, sizeof(imr_));
    w.WriteBytes("tps_int_pending", pending_, sizeof(pending_));
    w.WriteBytes("tps_int_edr", edr_, sizeof(edr_));
    w.Write("tps_int_sih_ctrl", sih_ctrl_);
    w.Write("tps_int_rtc_it", rtc_it_);
    w.Write("tps_int_int1", int1_);
}

void Tps65950Interrupts::Restore(StateReader& r) {
    r.ReadBytes("tps_int_isr", isr_, sizeof(isr_));
    r.ReadBytes("tps_int_imr", imr_, sizeof(imr_));
    r.ReadBytes("tps_int_pending", pending_, sizeof(pending_));
    r.ReadBytes("tps_int_edr", edr_, sizeof(edr_));
    r.Read("tps_int_sih_ctrl", sih_ctrl_);
    r.Read("tps_int_rtc_it", rtc_it_);
    r.Read("tps_int_int1", int1_);
}

void Tps65950Interrupts::PostRestore() {
    board_->DriveInt1(int1_);
}

REGISTER_SERVICE(Tps65950Interrupts);

}
