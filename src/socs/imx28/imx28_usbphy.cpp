#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_usbphy.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Section 32.3 memory map. */
constexpr uint32_t kPwd          = 0x00u;
constexpr uint32_t kTx           = 0x10u;
constexpr uint32_t kRx           = 0x20u;
constexpr uint32_t kCtrl         = 0x30u;
constexpr uint32_t kStatus       = 0x40u;
constexpr uint32_t kDebug        = 0x50u;
constexpr uint32_t kDebug0Status = 0x60u;
constexpr uint32_t kDebug1       = 0x70u;
constexpr uint32_t kVersion      = 0x80u;
constexpr uint32_t kMapEnd       = 0x90u;

/* Sections 32.3.1-32.3.6 and 32.3.8: register slots in memory-map order. */
enum Slot : uint32_t { kSlotPwd, kSlotTx, kSlotRx, kSlotCtrl, kSlotDebug, kSlotDebug1, kSlotCount };

struct SlotDef {
    uint32_t offset;
    uint32_t reset;
    uint32_t writable;
};

/* MCIMX28RM Rev. 2 section 32.3 memory map reset values; W-row fields of figures 32.3.1-32.3.8. */
constexpr SlotDef kSlotDefs[kSlotCount] = {
    {kPwd,    0x001E1C00u, 0x001E1C00u},
    {kTx,     0x10060607u, 0x1C0F0F0Fu},
    {kRx,     0x00000000u, 0x00400077u},
    {kCtrl,   0xC0200000u, 0xD1FBFFFFu},
    {kDebug,  0x7F180000u, 0x7F1F1F3Fu},
    {kDebug1, 0x00001000u, 0x00006000u},
};

/* Section 32.3.9 VERSION. */
constexpr uint32_t kVersionValue = 0x04020000u;

/* Section 32.3.4 CTRL fields. */
constexpr uint32_t kCtrlSuspendm        = 1u << 29;
constexpr uint32_t kCtrlWakeupIrq       = 1u << 17;
constexpr uint32_t kCtrlEnIrqWakeup     = 1u << 16;
constexpr uint32_t kCtrlDevpluginIrq    = 1u << 12;
constexpr uint32_t kCtrlEnIrqDevplugin  = 1u << 11;
constexpr uint32_t kCtrlResumeIrq       = 1u << 10;
constexpr uint32_t kCtrlEnIrqResume     = 1u << 9;
constexpr uint32_t kCtrlEnOtgIdDetect   = 1u << 7;
constexpr uint32_t kCtrlOtgIdChgIrq     = 1u << 6;
constexpr uint32_t kCtrlDevpluginPol    = 1u << 5;
constexpr uint32_t kCtrlEnDevpluginDet  = 1u << 4;
constexpr uint32_t kCtrlHostDisconIrq   = 1u << 3;
constexpr uint32_t kCtrlEnIrqHostDiscon = 1u << 2;
constexpr uint32_t kCtrlEnOtgIdChgIrq   = 1u << 0;

/* Section 32.3.1 PWD: the seven power-down bits; UTMI_SUSPENDM is 0 when all are set. */
constexpr uint32_t kPwdAll = 0x001E1C00u;

/* Table 5-1 sources 95 usb0_wakeup_irq and 94 usb1_wakeup_irq. */
constexpr uint32_t kWakeupSource[2] = {95u, 94u};

/* Section 39.3: _SET, _CLR and _TOG exist for the registers that are not read-only. */
bool HasSctAlias(uint32_t reg) {
    return reg == kPwd || reg == kTx || reg == kRx || reg == kCtrl || reg == kDebug ||
           reg == kDebug1;
}

uint32_t SlotOf(uint32_t reg) {
    for (uint32_t i = 0; i < kSlotCount; ++i) {
        if (kSlotDefs[i].offset == reg) return i;
    }
    return kSlotCount;
}

}  /* namespace */

static_assert(kSlotCount == 6u, "Imx28Usbphy::kSlots");

bool Imx28Usbphy::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Usbphy::OnReady() {
    icoll_ = &emu_.Get<Imx28Icoll>();
    LoadResetValues();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        LoadResetValues();
        DriveLine(false);
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Usbphy::LoadResetValues() {
    for (uint32_t i = 0; i < kSlotCount; ++i) regs_[i] = kSlotDefs[i].reset;
}

/* Section 32.3.4 UTMI_SUSPENDM: "If all the power-down bits in the USBPHYx_PWD are enabled,
   UTMI_SUSPENDM will be 0, otherwise 1". */
uint32_t Imx28Usbphy::ComposeCtrl() const {
    const bool all_down = (regs_[kSlotPwd] & kPwdAll) == kPwdAll;
    return regs_[kSlotCtrl] | (all_down ? 0u : kCtrlSuspendm);
}

/* Section 32.3.4: each enabled status bit raises the UTM interrupt of Table 5-1. */
void Imx28Usbphy::DriveLine(bool force) {
    const uint32_t c = regs_[kSlotCtrl];
    const bool on = ((c & kCtrlWakeupIrq) != 0u && (c & kCtrlEnIrqWakeup) != 0u) ||
                    ((c & kCtrlDevpluginIrq) != 0u && (c & kCtrlEnIrqDevplugin) != 0u) ||
                    ((c & kCtrlResumeIrq) != 0u && (c & kCtrlEnIrqResume) != 0u) ||
                    ((c & kCtrlOtgIdChgIrq) != 0u && (c & kCtrlEnOtgIdChgIrq) != 0u) ||
                    ((c & kCtrlHostDisconIrq) != 0u && (c & kCtrlEnIrqHostDiscon) != 0u);
    if (!force && on == line_) return;
    line_ = on;
    if (on) icoll_->AssertSource(kWakeupSource[unit_]);
    else    icoll_->DeassertSource(kWakeupSource[unit_]);
}

uint32_t Imx28Usbphy::ReadWord(uint32_t addr) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) {
        if (!HasSctAlias(a.reg_offset)) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    switch (a.reg_offset) {
        case kCtrl:         return ComposeCtrl();
        /* Section 32.3.5: no host or device on DP/DM; section 32.2.4.5 plugged-in detector 0. */
        case kStatus:       return 0u;
        case kDebug0Status: return 0u;
        case kVersion:      return kVersionValue;
        default:            break;
    }
    const uint32_t slot = SlotOf(a.reg_offset);
    if (slot == kSlotCount) HaltUnsupportedAccess("ReadWord", addr, 0);
    return regs_[slot];
}

void Imx28Usbphy::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = regs_[kSlotCtrl];
    uint32_t       next = Imx28ApplySct(op, old, value);
    next = (old & ~kSlotDefs[kSlotCtrl].writable) | (next & kSlotDefs[kSlotCtrl].writable);
    if (IsSoftReset(old, next)) {
        for (uint32_t i : {kSlotPwd, kSlotTx, kSlotRx, kSlotCtrl}) regs_[i] = kSlotDefs[i].reset;
        DriveLine(false);
        return;
    }
    if ((next & kCtrlEnOtgIdDetect) != 0u ||
        ((next & kCtrlEnDevpluginDet) != 0u && (next & kCtrlDevpluginPol) != 0u)) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    regs_[kSlotCtrl] = next;
    DriveLine(false);
}

void Imx28Usbphy::WriteSlot(uint32_t slot, Imx28SctOp op, uint32_t value, uint32_t addr) {
    if (slot == kSlotPwd) {
        RequireOperating("WriteWord", regs_[kSlotCtrl], addr, value);
    }
    const SlotDef& d = kSlotDefs[slot];
    regs_[slot] = (regs_[slot] & ~d.writable) | (Imx28ApplySct(op, regs_[slot], value) & d.writable);
}

void Imx28Usbphy::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write && !HasSctAlias(a.reg_offset)) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    if (a.reg_offset == kCtrl) {
        WriteCtrl(a.op, value, addr);
        return;
    }
    const uint32_t slot = SlotOf(a.reg_offset);
    if (slot == kSlotCount) HaltUnsupportedAccess("WriteWord", addr, value);
    WriteSlot(slot, a.op, value, addr);
}

void Imx28Usbphy::SaveState(StateWriter& w) { w.Write("regs", regs_); }

void Imx28Usbphy::RestoreState(StateReader& r) { r.Read("regs", regs_); }

}
