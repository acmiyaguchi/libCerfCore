#include "imx28_pinctrl.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_icoll.h"
#include "imx28_id.h"

namespace cerfcore {

namespace {

/* Section 9.4.1 CTRL at offset 0. */
constexpr uint32_t kCtrlOffset  = 0x000u;

/* Section 9.4.1 CTRL IRQOUT4-0 [4:0]: "Read-only view of the interrupt collector GPIOn signal". */
constexpr uint32_t kCtrlIrqOutMask = 0x1Fu;

/* Section 9.4 memory map: per-bank register arrays, one register per 0x10 bytes. */
constexpr uint32_t kMuxsel    = 0x100u;
constexpr uint32_t kDout      = 0x700u;
constexpr uint32_t kDin       = 0x900u;
constexpr uint32_t kDoe       = 0xB00u;
constexpr uint32_t kPin2Irq   = 0x1000u;
constexpr uint32_t kIrqEn     = 0x1100u;
constexpr uint32_t kIrqLevel  = 0x1200u;
constexpr uint32_t kIrqPol    = 0x1300u;
constexpr uint32_t kIrqStat   = 0x1400u;
constexpr uint32_t kMapEnd    = 0x1C00u;

/* Table 5-1 sources 127 (pinctrl0_irq) down to 123 (pinctrl4_irq): bank b is source 127 - b. */
constexpr uint32_t kIrqBank0  = 127u;

/* Section 9.2.2: MUXSELx two bits per pin, 3 = GPIO; banks 0-4 use MUXSEL(2b) and MUXSEL(2b+1)
   (section 9.4.2-9.4.11: bank pins 0-15 then 16-). */
constexpr uint32_t kMuxGpio   = 3u;

/* Section 9.2.1: Ethernet pins keep their state across software reset. Bank 4 pins 0-15
   (ENET0_* in MUXSEL8 field descriptions) and pin 16 (ENET_CLK, MUXSEL9). */
constexpr uint32_t kEnetPins = 0x0001FFFFu;
struct EnetRetain { uint32_t offset; uint32_t mask; };
constexpr EnetRetain kEnetRetain[] = {
    {0x180u, 0xFFFFFFFFu},   /* MUXSEL8 pins 0-15 */
    {0x190u, 0x00000003u},   /* MUXSEL9 pin 16 */
    {0x400u, 0xFFFFFFFFu},   /* DRIVE16 pins 0-7 */
    {0x410u, 0xFFFFFFFFu},   /* DRIVE17 pins 8-15 */
    {0x420u, 0x00000007u},   /* DRIVE18 pin 16 */
    {0x640u, kEnetPins},     /* PULL4 */
    {0x740u, kEnetPins},     /* DOUT4 */
    {0xB40u, kEnetPins},     /* DOE4 */
};

}  /* namespace */

bool Imx28Pinctrl::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Pinctrl::OnReady() {
    icoll_ = &emu_.Get<Imx28Icoll>();
    LoadResetValues(true);
    emu_.Get<GuestCpuReset>().RegisterResetListener(
        [this](ResetLineKind kind) { LoadResetValues(kind == ResetLineKind::Rtc); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* Section 9.4 memory map "Reset value"; section 9.2.1: pins come out of reset as GPIO inputs;
   "Ethernet pins ... will only reset when power is on. Software reset will not affect them." */
void Imx28Pinctrl::LoadResetValues(bool power_on) {
    uint32_t kept[sizeof(kEnetRetain) / sizeof(kEnetRetain[0])];
    for (uint32_t k = 0; k < sizeof(kept) / sizeof(kept[0]); ++k) {
        kept[k] = Reg(kEnetRetain[k].offset) & kEnetRetain[k].mask;
    }
    for (uint32_t i = 0; i < kImx28PinctrlRegCount; ++i) regs_[i] = kImx28PinctrlRegs[i].reset;
    for (uint32_t b = 0; b < kBanks; ++b) level_[b] = armed_[b] = 0u;
    if (!power_on) {
        for (uint32_t k = 0; k < sizeof(kept) / sizeof(kept[0]); ++k) {
            uint32_t& r = RegRef(kEnetRetain[k].offset);
            r = (r & ~kEnetRetain[k].mask) | kept[k];
        }
    }
    DriveLines(false);
}

void Imx28Pinctrl::SetPinLevelProvider(PinLevelFn fn) {
    if (pin_level_) emu_.Get<Fatal>().Die("Imx28Pinctrl: second pin level provider");
    pin_level_ = std::move(fn);
}

uint32_t Imx28Pinctrl::MuxSelect(uint32_t bank, uint32_t pin) const {
    return (Reg(kMuxsel + (bank * 2u + pin / 16u) * 0x10u) >> ((pin % 16u) * 2u)) & 0x3u;
}

uint32_t Imx28Pinctrl::DecodeRow(const char* op, uint32_t addr, uint32_t value,
                                 Imx28SctOp* sct_op) const {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess(op, addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    for (uint32_t i = 0; i < kImx28PinctrlRegCount; ++i) {
        if (kImx28PinctrlRegs[i].offset != a.reg_offset) continue;
        if (a.op != Imx28SctOp::Write && !kImx28PinctrlRegs[i].sct) {
            HaltUnsupportedAccess(op, addr, value);
        }
        *sct_op = a.op;
        return i;
    }
    HaltUnsupportedAccess(op, addr, value);
}

uint32_t& Imx28Pinctrl::RegRef(uint32_t offset) {
    for (uint32_t i = 0; i < kImx28PinctrlRegCount; ++i) {
        if (kImx28PinctrlRegs[i].offset == offset) return regs_[i];
    }
    emu_.Get<Fatal>().Die("Imx28Pinctrl: no register at offset %03X", offset);
}

uint32_t Imx28Pinctrl::Reg(uint32_t offset) const {
    for (uint32_t i = 0; i < kImx28PinctrlRegCount; ++i) {
        if (kImx28PinctrlRegs[i].offset == offset) return regs_[i];
    }
    emu_.Get<Fatal>().Die("Imx28Pinctrl: no register at offset %03X", offset);
}

/* MCIMX28RM section 9.2.3.1 "Once set, the logic value the HW_PINCTRL_DOUTx bit will be driven
   on the pin"; section 9.4.48 DIN "accurately reflects the state of the pin regardless of the
   setting of the HW_PINCTRL_MUXSELx or HW_PINCTRL_DOEx registers", reset 0. */
uint32_t Imx28Pinctrl::BankLevels(uint32_t bank, uint32_t* known) const {
    const uint32_t dout  = Reg(kDout + bank * 0x10u);
    const uint32_t doe   = Reg(kDoe + bank * 0x10u);
    const uint32_t valid = kImx28PinctrlPinMask[bank];
    uint32_t level = 0u, k = 0u;
    for (uint32_t pin = 0; pin < 32u; ++pin) {
        const uint32_t bit = 1u << pin;
        if ((valid & bit) == 0u) continue;
        if (MuxSelect(bank, pin) == kMuxGpio && (doe & bit) != 0u) {
            k |= bit;
            if ((dout & bit) != 0u) level |= bit;
            continue;
        }
        bool high = false;
        if (pin_level_ && pin_level_(bank, pin, &high)) {
            k |= bit;
            if (high) level |= bit;
        }
    }
    *known = k;
    return level;
}

void Imx28Pinctrl::NotifyPinLevelChanged(uint32_t bank, uint32_t pin) {
    if (bank >= kBanks || pin >= 32u || (kImx28PinctrlPinMask[bank] & (1u << pin)) == 0u) {
        emu_.Get<Fatal>().Die("Imx28Pinctrl: level change on nonexistent bank %u pin %u", bank, pin);
    }
    UpdateBanks();
}

bool Imx28Pinctrl::SamplePin(uint32_t bank, uint32_t pin, const char* who) const {
    uint32_t known = 0u;
    const uint32_t level = BankLevels(bank, &known);
    if ((known & (1u << pin)) == 0u) {
        emu_.Get<Fatal>().Die("Imx28Pinctrl: %s samples bank %u pin %u (MUXSEL %u) with no pin "
                              "level model", who, bank, pin, MuxSelect(bank, pin));
    }
    return (level & (1u << pin)) != 0u;
}

/* Section 9.2.3.3 / figure 9-5 IRQLEVEL, IRQPOL; section 9.4.78 level status bits "cannot be
   cleared unless either the actual pin is in the non-interrupting state, or the pin has been
   disabled as an interrupt source". */
void Imx28Pinctrl::UpdateBanks() {
    for (uint32_t b = 0; b < kBanks; ++b) {
        const uint32_t sel = Reg(kPin2Irq + b * 0x10u);
        uint32_t known = 0u;
        const uint32_t level = BankLevels(b, &known);
        if ((sel & ~known) != 0u) {
            emu_.Get<Fatal>().Die("Imx28Pinctrl: bank %u interrupt pins %08X have no pin level model",
                                  b, sel & ~known);
        }
        const uint32_t lvl  = Reg(kIrqLevel + b * 0x10u);
        const uint32_t pol  = Reg(kIrqPol + b * 0x10u);
        const uint32_t prev = level_[b];
        const uint32_t level_hit = lvl & ~(level ^ pol);
        const uint32_t rise      = ~lvl & pol & level & ~prev & armed_[b];
        const uint32_t fall      = ~lvl & ~pol & ~level & prev & armed_[b];
        RegRef(kIrqStat + b * 0x10u) |= sel & (level_hit | rise | fall);
        level_[b] = (level_[b] & ~sel) | (level & sel);
        armed_[b] = sel;
    }
    DriveLines(false);
}

/* Section 9.2.3.3: IRQSTAT & IRQEN raises the bank's interrupt; Table 5-1 maps bank b to ICOLL
   source 127 - b. */
void Imx28Pinctrl::DriveLines(bool force) {
    for (uint32_t b = 0; b < kBanks; ++b) {
        const bool level = (Reg(kIrqStat + b * 0x10u) & Reg(kIrqEn + b * 0x10u)) != 0u;
        if (!force && level == (((line_ >> b) & 1u) != 0u)) continue;
        line_ = (line_ & ~(1u << b)) | (level ? 1u << b : 0u);
        if (level) icoll_->AssertSource(kIrqBank0 - b);
        else       icoll_->DeassertSource(kIrqBank0 - b);
    }
}

void Imx28Pinctrl::PostRestore() { DriveLines(true); }

uint32_t Imx28Pinctrl::ComposeRead(uint32_t row, uint32_t lanes) {
    const uint32_t off = kImx28PinctrlRegs[row].offset;
    if (off >= kDin && off < kDin + kBanks * 0x10u) {
        const uint32_t bank = (off - kDin) / 0x10u;
        uint32_t known = 0u;
        const uint32_t level = BankLevels(bank, &known);
        const uint32_t missing = kImx28PinctrlPinMask[bank] & lanes & ~known;
        if (missing != 0u) {
            emu_.Get<Fatal>().Die("Imx28Pinctrl: DIN%u read needs pins %08X with no pin level model",
                                  bank, missing);
        }
        return level;
    }
    if (off == kCtrlOffset) {
        uint32_t irqout = 0u;
        for (uint32_t b = 0; b < kBanks; ++b) {
            if ((Reg(kIrqStat + b * 0x10u) & Reg(kIrqEn + b * 0x10u)) != 0u) irqout |= 1u << b;
        }
        return (regs_[row] & ~kCtrlIrqOutMask) | irqout;
    }
    return regs_[row];
}

uint32_t Imx28Pinctrl::ReadLanes(uint32_t word_addr, uint32_t lanes) {
    Imx28SctOp op;
    const uint32_t row = DecodeRow("Read", word_addr, 0, &op);
    if (op != Imx28SctOp::Write) return 0u;   /* RM 39.3: SCT registers read back 0 */
    if (kImx28PinctrlRegs[row].offset != kCtrlOffset) {
        RequireOperating("Read", Reg(kCtrlOffset), word_addr, 0);
    }
    return ComposeRead(row, lanes);
}

uint32_t Imx28Pinctrl::PeekLanes(uint32_t word_addr, uint32_t lanes) {
    Imx28SctOp op;
    const uint32_t row = DecodeRow("WriteWord", word_addr, 0, &op);
    if (kImx28PinctrlRegs[row].offset != kCtrlOffset) {
        RequireOperating("WriteWord", Reg(kCtrlOffset), word_addr, 0);
    }
    return ComposeRead(row, lanes);
}

uint32_t Imx28Pinctrl::ReadWord(uint32_t addr) { return ReadLanes(addr, 0xFFFFFFFFu); }

/* Figure 9-4 "Write zero to HW_PINCTRL_IRQSTATx register bit to clear interrupts"; section 9.4.78
   "Software may clear any bit in this register by writing a 1 to the bit at the SCT clear
   address". */
void Imx28Pinctrl::WriteWord(uint32_t addr, uint32_t value) {
    Imx28SctOp op;
    const uint32_t row = DecodeRow("WriteWord", addr, value, &op);
    const Imx28PinctrlReg& def = kImx28PinctrlRegs[row];
    const uint32_t off = def.offset;
    if (def.writable == 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    if (off != kCtrlOffset) RequireOperating("WriteWord", Reg(kCtrlOffset), addr, value);
    const uint32_t old = regs_[row];
    const uint32_t next = Imx28ApplySct(op, old, value);
    regs_[row] = (old & ~def.writable) | (next & def.writable);
    if (off == kCtrlOffset && (regs_[row] & kCtrlSftrst) != 0u) {
        /* Section 9.4.1 SFTRST "forces a block-level reset"; section 9.3 "The reset process
           gates the clocks automatically". */
        LoadResetValues(false);
        return;
    }
    if (off != kCtrlOffset) UpdateBanks();
}

void Imx28Pinctrl::SaveState(StateWriter& w) {
    w.WriteBytes("regs", regs_, sizeof(regs_));
    w.WriteBytes("level", level_, sizeof(level_));
    w.WriteBytes("armed", armed_, sizeof(armed_));
}

void Imx28Pinctrl::RestoreState(StateReader& r) {
    r.ReadBytes("regs", regs_, sizeof(regs_));
    r.ReadBytes("level", level_, sizeof(level_));
    r.ReadBytes("armed", armed_, sizeof(armed_));
}

REGISTER_SERVICE(Imx28Pinctrl);

}
