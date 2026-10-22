#include "imx28_power.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_clkctrl.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_lradc_inputs.h"

#include <algorithm>
#include <mutex>

namespace cerfcore {

namespace {

/* Section 11.12.1 CTRL: interrupt status and enable bits. */
constexpr uint32_t kCtrlEnVdd5vGt     = 1u << 0;
constexpr uint32_t kCtrlVdd5vGtIrq    = 1u << 1;
constexpr uint32_t kCtrlPolVdd5vGt    = 1u << 2;
constexpr uint32_t kCtrlEnVbusValid   = 1u << 3;
constexpr uint32_t kCtrlVbusValidIrq  = 1u << 4;
constexpr uint32_t kCtrlPolVbusValid  = 1u << 5;
constexpr uint32_t kCtrlEnVdddBo      = 1u << 6;
constexpr uint32_t kCtrlVdddBoIrq     = 1u << 7;
constexpr uint32_t kCtrlEnVddaBo      = 1u << 8;
constexpr uint32_t kCtrlVddaBoIrq     = 1u << 9;
constexpr uint32_t kCtrlEnVddioBo     = 1u << 10;
constexpr uint32_t kCtrlVddioBoIrq    = 1u << 11;
constexpr uint32_t kCtrlEnBattBo      = 1u << 12;
constexpr uint32_t kCtrlBattBoIrq     = 1u << 13;
constexpr uint32_t kCtrlEnDcOk        = 1u << 14;
constexpr uint32_t kCtrlDcOkIrq       = 1u << 15;
constexpr uint32_t kCtrlPolDcOk       = 1u << 16;
constexpr uint32_t kCtrlEnPswitch     = 1u << 17;
constexpr uint32_t kCtrlPswitchIrq    = 1u << 20;
constexpr uint32_t kCtrlPolPswitch    = 1u << 18;
constexpr uint32_t kCtrlPswitchIrqSrc = 1u << 19;
constexpr uint32_t kCtrlEnVdd5vDroop  = 1u << 21;
constexpr uint32_t kCtrlVdd5vDroopIrq = 1u << 22;
constexpr uint32_t kCtrlEn4p2Bo       = 1u << 23;
constexpr uint32_t kCtrl4p2BoIrq      = 1u << 24;
constexpr uint32_t kCtrlPswitchMidTran = 1u << 27;

/* Table 5-1 sources 0-6 and the shared line of section 11.11. */
constexpr uint32_t kLineBatt = 0u, kLineVddd = 1u, kLineVddio = 2u, kLineVdda = 3u,
                   kLineDroop = 4u, kLine4p2 = 5u, kLineShared = 6u;
constexpr uint32_t kLineCount = 7u;

/* Section 11.12.2 5VCTRL fields. */
constexpr uint32_t kVctrlVbusDroopShift = 28u;
constexpr uint32_t kVctrlPwdCharge4p2   = 1u << 20;
constexpr uint32_t kVctrlVbusValidShift = 8u;
constexpr uint32_t kVctrlPwdn5vBrnout   = 1u << 7;
constexpr uint32_t kVctrlVbusValid5vDet = 1u << 4;
constexpr uint32_t kVctrlVbusValidToB   = 1u << 3;
constexpr uint32_t kVctrlPwrupVbusCmps  = 1u << 1;
constexpr uint32_t kThreshold3          = 0x7u;
constexpr uint32_t kVbusValidMv[8] = {2900u, 4000u, 4100u, 4200u, 4300u, 4400u, 4500u, 4600u};
constexpr uint32_t kVbusDroopMv[4] = {4300u, 4400u, 4500u, 4700u};

/* Section 11.12.13 STS: thresholds in the field descriptions. */
constexpr uint32_t kStsAvalidMv      = 2000u;
constexpr uint32_t kStsBvalidMv      = 4000u;
constexpr uint32_t kStsSessendMv     = 200u;
constexpr uint32_t kStsVddioMarginMv = 500u;
constexpr uint32_t kStsDcOk          = 1u << 9;
constexpr uint32_t kStsPswitchShift  = 20u;
constexpr uint32_t kStsVdd5vFault    = 1u << 12;
constexpr uint32_t kStsDcdc4p2Bo     = 1u << 10;

/* Section 11.12.9 DCDC4P2: ENABLE_4P2 [23], TRG [18:16] 4.2 V less 0.1 V per step or BATTERY at
   0b1XX, BO [12:8] 3.6 V plus 25 mV per step. */
constexpr uint32_t kDcdc4p2Enable4p2 = 1u << 23;
constexpr uint32_t kDcdc4p2TrgShift  = 16u;
constexpr uint32_t kDcdc4p2TrgBatt   = 0x4u;
constexpr uint32_t kDcdc4p2BoShift   = 8u;
constexpr uint32_t kDcdc4p2BoMask    = 0x1Fu;
constexpr uint32_t k4p2TrgTopMv      = 4200u;
constexpr uint32_t k4p2TrgStepMv     = 100u;
constexpr uint32_t k4p2BoBaseMv      = 3600u;
constexpr uint32_t k4p2BoStepMv      = 25u;

/* Section 11.12.7 VDDIOCTRL TRG: 2.8 V plus 50 mV per step, codes 0x10-0x1F map to 3.6 V. */
constexpr uint32_t kVddioBaseMv   = 2800u;
constexpr uint32_t kVddioStepMv   = 50u;
constexpr uint32_t kVddioMaxCode  = 0x10u;
constexpr uint32_t kTrgMask       = 0x1Fu;

/* Section 11.12.15 BATTMONITOR: BATT_VAL [25:16], BRWNOUT_PWD [8], BRWNOUT_LVL [4:0]; brownout
   voltage 2.4 V + 0.04 V * BRWNOUT_LVL. */
constexpr uint32_t kBattValMask    = 0x03FF0000u;
constexpr uint32_t kBattValShift   = 16u;
constexpr uint32_t kBattBrwnoutPwd = 1u << 8;
constexpr uint32_t kBattLvlMask    = 0x1Fu;
constexpr uint32_t kBattBrwnoutMv  = 2400u;
constexpr uint32_t kBattLvlStepMv  = 40u;

/* Section 11.12.4 CHARGE PWD_BATTCHRG [16]; section 11.12.18 THERMAL PWD [7]; section 11.12.14
   SPEED CTRL [1:0]. */
constexpr uint32_t kChargePwdBattChrg  = 1u << 16;
constexpr uint32_t kThermalPwd         = 1u << 7;
constexpr uint32_t kSpeedCtrl          = 0x3u;

/* Section 11.12.16 RESET: UNLOCK key 0x3E77 [31:16], PWD_OFF [1], PWD [0]. */
constexpr uint32_t kResetKey      = 0x3E77u;
constexpr uint32_t kResetPwdOff   = 1u << 1;
constexpr uint32_t kResetPwd      = 1u << 0;
constexpr uint32_t kResetLowMask  = 0x7u;

/* Section 11.12.19 USB1CTRL AVALID1 [3], BVALID1 [2], VBUSVALID1 [1], SESSEND1 [0]. */
constexpr uint32_t kUsb1Avalid    = 1u << 3;
constexpr uint32_t kUsb1Bvalid    = 1u << 2;
constexpr uint32_t kUsb1Vbusvalid = 1u << 1;
constexpr uint32_t kUsb1Sessend   = 1u << 0;

/* Section 11.3.3: power-up starts once PSWITCH >= MID level or VDD5V > 4.25 V lasts 100 ms. */
constexpr uint32_t kPowerUpVdd5vMv = 4250u;
constexpr int64_t  kPowerUpHoldNs  = 100000000;
constexpr int64_t  kNoPowerUpEvent = -1;

bool HasSctAlias(uint32_t index) { return index != kPwrSts && index != kPwrVersion; }

}  /* namespace */

bool Imx28Power::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Power::OnReady() {
    icoll_  = &emu_.Get<Imx28Icoll>();
    inputs_ = emu_.TryGet<Imx28LradcInputs>();
    if (inputs_ == nullptr) emu_.Get<Fatal>().Die("Imx28Power: no analog input model");
    Reset();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (!emu_.Get<Imx28Clkctrl>().ConsumeDigitalOnlyReset()) Reset();
        powered_down_ = false;
        DriveLines(false);
    });
    inputs_->RegisterChangeListener([this] { Refresh(); });
    auto& sleep = emu_.Get<GuestDeepSleep>();
    sleep_ = &sleep;
    sleep.RegisterSleepEntryListener([this] { EnterPoweredDown(); });
    sleep.RegisterParkWakeSource([this] { return PowerUpDue(); });
    sleep.RegisterParkWakeDue([this] { return PowerUpDueNs(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* Section 11.12 memory map, Reset value column; section 11.3.5: a chip reset resets the power
   module control bits. */
void Imx28Power::Reset() {
    bool changed[2] = {};
    {
        std::lock_guard<std::mutex> guard(mtx_);
        for (uint32_t i = 0; i < kPwrRegCount; ++i) regs_[i] = kImx28PowerRegs[i].reset;
        const Signals s = ComputeSignals(SampleAnalog());
        LatchInterrupts(s);
        vdd5v_gt_vddio_ = s.vdd5v_gt_vddio;
        for (uint32_t u = 0; u < 2u; ++u) {
            const UsbSession now = SessionOf(u, s);
            changed[u]   = !(now == sessions_[u]);
            sessions_[u] = now;
        }
    }
    for (uint32_t u = 0; u < 2u; ++u) {
        if (!changed[u]) continue;
        for (auto& fn : session_listeners_[u]) fn();
    }
}

Imx28Power::Analog Imx28Power::SampleAnalog() {
    const uint32_t code = std::min(regs_[kPwrVddioCtrl] & kTrgMask, kVddioMaxCode);
    return Analog{inputs_->Vdd5vMillivolts(), inputs_->BatteryMillivolts(),
                  kVddioBaseMv + kVddioStepMv * code};
}

/* Section 11.12.13 STS field descriptions and section 11.12.2 comparator enables. */
Imx28Power::Signals Imx28Power::ComputeSignals(const Analog& a) const {
    const uint32_t vctrl = regs_[kPwr5vCtrl];
    const uint32_t v5    = a.vdd5v_mv;
    const bool     cmps  = (vctrl & kVctrlPwrupVbusCmps) != 0u;
    Signals s{};
    s.vdd5v_gt_vddio = v5 > a.vddio_mv + kStsVddioMarginMv;
    s.vbusvalid = (vctrl & kVctrlVbusValid5vDet) != 0u &&
                  v5 > kVbusValidMv[(vctrl >> kVctrlVbusValidShift) & kThreshold3];
    s.bvalid  = (vctrl & kVctrlVbusValidToB) != 0u ? s.vbusvalid : cmps && v5 > kStsBvalidMv;
    s.avalid  = cmps && v5 > kStsAvalidMv;
    s.sessend = cmps && v5 < kStsSessendMv;
    s.droop   = v5 < kVbusDroopMv[(vctrl >> kVctrlVbusDroopShift) & 0x3u];
    const uint32_t bm = regs_[kPwrBattMonitor];
    s.batt_bo = (bm & kBattBrwnoutPwd) == 0u &&
                a.batt_mv < kBattBrwnoutMv + kBattLvlStepMv * (bm & kBattLvlMask);
    return s;
}

/* Section 11.12.13 STS: [21:20] PSWITCH; [3:0] and the STATUS copies [17:14]; VDD5V_FAULT [12]
   asserts when 5V is below roughly VDDIO/2 with the charger powered down. */
uint32_t Imx28Power::ComposeSts() {
    const Analog  a = SampleAnalog();
    const Signals s = ComputeSignals(a);
    uint32_t sts = kStsDcOk | (pswitch_ << kStsPswitchShift);
    if (s.sessend)   sts |= (1u << 0) | (1u << 14);
    if (s.vbusvalid) sts |= (1u << 1) | (1u << 15);
    if (s.bvalid)    sts |= (1u << 2) | (1u << 16);
    if (s.avalid)    sts |= (1u << 3) | (1u << 17);
    if (s.droop)     sts |= 1u << 4;
    if (s.vdd5v_gt_vddio) sts |= 1u << 5;
    if (s.batt_bo)   sts |= 1u << 13;
    if (a.vdd5v_mv < a.vddio_mv / 2u) sts |= kStsVdd5vFault;
    if (Vdd4p2Millivolts(a) < k4p2BoBaseMv + k4p2BoStepMv *
                                  ((regs_[kPwrDcdc4p2] >> kDcdc4p2BoShift) & kDcdc4p2BoMask)) {
        sts |= kStsDcdc4p2Bo;
    }
    return sts;
}

/* Section 11.12.2 PWD_CHARGE_4P2 [20] and section 11.12.9 ENABLE_4P2, TRG: the 4.2 V regulation
   circuit drives the DCDC_4P2 pin from 5 V. */
uint32_t Imx28Power::Vdd4p2Millivolts(const Analog& a) const {
    const uint32_t d = regs_[kPwrDcdc4p2];
    if ((regs_[kPwr5vCtrl] & kVctrlPwdCharge4p2) != 0u || (d & kDcdc4p2Enable4p2) == 0u) return 0u;
    const uint32_t trg    = (d >> kDcdc4p2TrgShift) & 0x7u;
    const uint32_t target = (trg & kDcdc4p2TrgBatt) != 0u ? a.batt_mv
                                                          : k4p2TrgTopMv - k4p2TrgStepMv * trg;
    return std::min(target, a.vdd5v_mv);
}

/* Section 11.12.1 CTRL: each interrupt status bit is set while its source matches the polarity
   field; PSWITCH_IRQ_SRC picks STS PSWITCH bit 1 or bit 0. */
void Imx28Power::LatchInterrupts(const Signals& s) {
    uint32_t& c = regs_[kPwrCtrl];
    if (s.batt_bo) c |= kCtrlBattBoIrq;
    if (s.droop)   c |= kCtrlVdd5vDroopIrq;
    if (s.vdd5v_gt_vddio == ((c & kCtrlPolVdd5vGt) != 0u)) c |= kCtrlVdd5vGtIrq;
    if (s.vbusvalid == ((c & kCtrlPolVbusValid) != 0u)) c |= kCtrlVbusValidIrq;
    const bool dc_ok_cond = (c & kCtrlPolDcOk) != 0u ? true : !s.vdd5v_gt_vddio;
    if (dc_ok_cond) c |= kCtrlDcOkIrq;
    if ((c & kCtrlPswitchMidTran) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Power: CTRL %08X sets PSWITCH_MID_TRAN", c);
    }
    const uint32_t src = (c & kCtrlPswitchIrqSrc) != 0u ? (pswitch_ >> 1) & 1u : pswitch_ & 1u;
    if ((src != 0u) == ((c & kCtrlPolPswitch) != 0u)) c |= kCtrlPswitchIrq;
}

void Imx28Power::DriveLines(bool force) {
    std::lock_guard<std::mutex> guard(mtx_);
    uint32_t want;
    {
        const uint32_t c = regs_[kPwrCtrl];
        const auto on = [c](uint32_t status, uint32_t enable) {
            return (c & status) != 0u && (c & enable) != 0u;
        };
        want = 0u;
        if (on(kCtrlBattBoIrq, kCtrlEnBattBo))       want |= 1u << kLineBatt;
        if (on(kCtrlVdddBoIrq, kCtrlEnVdddBo))       want |= 1u << kLineVddd;
        if (on(kCtrlVddioBoIrq, kCtrlEnVddioBo))     want |= 1u << kLineVddio;
        if (on(kCtrlVddaBoIrq, kCtrlEnVddaBo))       want |= 1u << kLineVdda;
        if (on(kCtrlVdd5vDroopIrq, kCtrlEnVdd5vDroop)) want |= 1u << kLineDroop;
        if (on(kCtrl4p2BoIrq, kCtrlEn4p2Bo))         want |= 1u << kLine4p2;
        if (on(kCtrlVdd5vGtIrq, kCtrlEnVdd5vGt) || on(kCtrlDcOkIrq, kCtrlEnDcOk) ||
            on(kCtrlVbusValidIrq, kCtrlEnVbusValid) || on(kCtrlPswitchIrq, kCtrlEnPswitch)) {
            want |= 1u << kLineShared;
        }
    }
    for (uint32_t l = 0; l < kLineCount; ++l) {
        const bool on   = ((want >> l) & 1u) != 0u;
        const bool held = ((lines_ >> l) & 1u) != 0u;
        if (!force && on == held) continue;
        if (on) icoll_->AssertSource(l);
        else    icoll_->DeassertSource(l);
    }
    lines_ = want;
}

/* Section 11.2.1.1 Figure 11-2: a falling edge of STS VDD5V_GT_VDDIO with PWDN_5VBRNOUT set powers
   the chip off; section 11.3.4: RESET.PWD_OFF disables that path. */
void Imx28Power::Refresh() {
    bool changed[2] = {};
    bool down = false;
    {
        std::lock_guard<std::mutex> guard(mtx_);
        const Signals s = ComputeSignals(SampleAnalog());
        LatchInterrupts(s);
        for (uint32_t u = 0; u < 2u; ++u) {
            const UsbSession now = SessionOf(u, s);
            changed[u]  = !(now == sessions_[u]);
            sessions_[u] = now;
        }
        down = vdd5v_gt_vddio_ && !s.vdd5v_gt_vddio &&
               (regs_[kPwr5vCtrl] & kVctrlPwdn5vBrnout) != 0u &&
               (regs_[kPwrReset] & kResetPwdOff) == 0u;
        vdd5v_gt_vddio_ = s.vdd5v_gt_vddio;
    }
    DriveLines(false);
    for (uint32_t u = 0; u < 2u; ++u) {
        if (!changed[u]) continue;
        for (auto& fn : session_listeners_[u]) fn();
    }
    if (down) emu_.Get<GuestDeepSleep>().Enter();
}

/* Sections 11.3.3, 11.3.4.1: from the off state a power-up event lasting 100 ms powers up the chip. */
bool Imx28Power::PowerUpEvent() {
    std::lock_guard<std::mutex> guard(mtx_);
    return (pswitch_ & 0x1u) != 0u || inputs_->Vdd5vMillivolts() > kPowerUpVdd5vMv;
}

void Imx28Power::EnterPoweredDown() {
    powered_down_ = true;
    pwd_sleep_.store(pwd_write_.load(std::memory_order_acquire), std::memory_order_release);
    event_since_ns_ = PowerUpEvent() ? sleep_->SleptNs() : kNoPowerUpEvent;
}

bool Imx28Power::PowerUpDue() {
    if (!powered_down_) return false;
    if (!PowerUpEvent()) {
        event_since_ns_ = kNoPowerUpEvent;
        return false;
    }
    const int64_t now = sleep_->SleptNs();
    if (event_since_ns_ == kNoPowerUpEvent) event_since_ns_ = now;
    if (now - event_since_ns_ < kPowerUpHoldNs) return false;
    powered_down_ = false;
    return true;
}

int64_t Imx28Power::PowerUpDueNs() {
    if (!powered_down_ || event_since_ns_ == kNoPowerUpEvent) return GuestDeepSleep::kNoParkWake;
    return event_since_ns_ + kPowerUpHoldNs;
}

void Imx28Power::SetPswitchLevel(uint32_t level) {
    {
        std::lock_guard<std::mutex> guard(mtx_);
        pswitch_ = level & 0x3u;
    }
    Refresh();
}

void Imx28Power::LoadBattVal(uint32_t scaled) {
    std::lock_guard<std::mutex> guard(mtx_);
    regs_[kPwrBattMonitor] = (regs_[kPwrBattMonitor] & ~kBattValMask) |
                             ((scaled << kBattValShift) & kBattValMask);
}

Imx28Power::UsbSession Imx28Power::SessionOf(uint32_t unit, const Signals& s) const {
    if (unit == 0u) return UsbSession{s.avalid, s.bvalid, s.vbusvalid, s.sessend};
    const uint32_t u = regs_[kPwrUsb1Ctrl];
    return UsbSession{(u & kUsb1Avalid) != 0u, (u & kUsb1Bvalid) != 0u,
                      (u & kUsb1Vbusvalid) != 0u, (u & kUsb1Sessend) != 0u};
}

Imx28Power::UsbSession Imx28Power::Session(uint32_t unit) {
    std::lock_guard<std::mutex> guard(mtx_);
    return SessionOf(unit, ComputeSignals(SampleAnalog()));
}

uint32_t Imx28Power::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= (kPwrRegCount << 4) || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const uint32_t index = a.reg_offset >> 4;
    if (index == kPwrGap0F) HaltUnsupportedAccess("ReadWord", addr, 0);
    if (a.op != Imx28SctOp::Write) {
        if (!HasSctAlias(index)) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    std::lock_guard<std::mutex> guard(mtx_);
    return index == kPwrSts ? ComposeSts() : regs_[index];
}

/* Section 11.12.16: the lower bits take a write only while UNLOCK holds 0x3E77 (sharp_brain_pw_sh4
   nk.exe 8020ed80: SCT SET of the key, then CLR/SET of the low bits); section 11.3.4: PWD powers
   down the chip and PWD_OFF disables the power-down paths. */
void Imx28Power::WriteReset(Imx28SctOp op, uint32_t value) {
    bool down = false;
    {
        std::lock_guard<std::mutex> guard(mtx_);
        const uint32_t old  = regs_[kPwrReset];
        const uint32_t next = Imx28ApplySct(op, old, value);
        const bool     key  = (next >> 16) == kResetKey;
        regs_[kPwrReset] = (next & 0xFFFF0000u) |
                           ((key ? next : old) & kResetLowMask);
        const uint32_t r = regs_[kPwrReset];
        down = key && (r & kResetPwd) != 0u && (r & kResetPwdOff) == 0u;
    }
    if (!down) return;
    pwd_write_.store(true, std::memory_order_release);
    emu_.Get<GuestDeepSleep>().Enter();
    pwd_write_.store(false, std::memory_order_release);
}

void Imx28Power::WriteReg(uint32_t index, Imx28SctOp op, uint32_t value, uint32_t addr) {
    {
        std::lock_guard<std::mutex> guard(mtx_);
        const Imx28PowerReg& def = kImx28PowerRegs[index];
        const uint32_t old  = regs_[index];
        const uint32_t next = (old & ~def.writable) | (Imx28ApplySct(op, old, value) & def.writable);
        const auto die = [&](const char* what) {
            emu_.Get<Fatal>().Die("Imx28Power: %s at %08X (value %08X, register %08X -> %08X)", what,
                                  addr, value, old, next);
        };
        if (index == kPwrCharge && (old & kChargePwdBattChrg) != 0u &&
            (next & kChargePwdBattChrg) == 0u) {
            die("battery charger enable");
        }
        if (index == kPwrThermal && (next & kThermalPwd) == 0u) die("thermal sensor enable");
        if (index == kPwrSpeed && (next & kSpeedCtrl) != 0u) die("speed sensor control");
        regs_[index] = next;
    }
    Refresh();
}

void Imx28Power::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= (kPwrRegCount << 4) || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const uint32_t index = a.reg_offset >> 4;
    if (index == kPwrGap0F || !HasSctAlias(index)) HaltUnsupportedAccess("WriteWord", addr, value);
    if (index == kPwrReset) {
        WriteReset(a.op, value);
        return;
    }
    WriteReg(index, a.op, value, addr);
}

void Imx28Power::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> guard(mtx_);
    w.Write("regs", regs_);
    w.Write("lines", lines_);
    w.Write("pswitch", pswitch_);
    w.Write<uint8_t>("pwd_sleep", pwd_sleep_.load() ? 1u : 0u);
}

void Imx28Power::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> guard(mtx_);
    r.Read("regs", regs_);
    r.Read("lines", lines_);
    r.Read("pswitch", pswitch_);
    uint8_t pwd_sleep = 0;
    r.Read("pwd_sleep", pwd_sleep);
    pwd_sleep_.store(pwd_sleep != 0);
    const Signals s = ComputeSignals(SampleAnalog());
    vdd5v_gt_vddio_ = s.vdd5v_gt_vddio;
    for (uint32_t u = 0; u < 2u; ++u) sessions_[u] = SessionOf(u, s);
}

void Imx28Power::PostRestore() { DriveLines(true); }

REGISTER_SERVICE(Imx28Power);

}
