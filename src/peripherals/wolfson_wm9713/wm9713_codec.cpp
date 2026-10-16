#include "wm9713_codec.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/symbol_hamptons/symbol_hamptons_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/state/state_stream.h>
#include "wm9713_clocks.h"

namespace cerfcore {

namespace {

/* symbol_mk500 touch.dll FUN_02236e30 @0x02236e30 requires reg 0x7C == 0x574D
   and reg 0x7E == 0x4C13, then decodes reg 0x5A & 0x1C as the revision index
   (0 -> 'A', 4 -> 'B', 8 -> 'C'). */
constexpr uint32_t kReg5A        = 0x5Au;
constexpr uint32_t kRegVendorId1 = 0x7Cu;
constexpr uint32_t kRegVendorId2 = 0x7Eu;
constexpr uint16_t kWm97xxId1    = 0x574Du;
constexpr uint16_t kWm9713Id2    = 0x4C13u;
constexpr uint16_t kRevisionC    = 0x0008u;

/* Cirrus Logic WM9713L Rev 4.0 Table 68 (page 91): 74h POLL 9, CTC 8, ADCSEL 7:1; 76h CR 9:8, DEL 7:4,
   SLEN 3; 78h PRP 15:14, PDEN 11; Table 47 (page 70): 3Ch PADCPD bit 15, "The pen ADC is powered-down
   using PADCPD". */
constexpr uint32_t kRegDigitiser1  = 0x74u;
constexpr uint32_t kRegDigitiser2  = 0x76u;
constexpr uint32_t kRegDigitiserRd = 0x7Au;
constexpr uint16_t kPoll = 1u << 9, kCtc = 1u << 8, kSelMask = 0x00FEu;
/* Cirrus Logic WM9713L Rev 4.0 Table 49 (page 72): 74h COO bit 0 "1 = Co-ordinate measurement". */
constexpr uint16_t kCoo  = 1u << 0;
constexpr uint16_t kPden = 1u << 11;
constexpr uint16_t kPadcpd = 1u << 15;
/* Cirrus Logic WM9713L Rev 4.0 Table 48 (page 71): CR 00 every 512, 01 every 400, 10 every 312, 11
   every 256 AC-Link frames; Table 50 (page 73): ADCSRC 001 X, 010 Y, 011 pressure. */
constexpr uint32_t kCrFrames[4] = {512u, 400u, 312u, 256u};
constexpr uint8_t  kSrcX = 1u, kSrcY = 2u, kSrcPressure = 3u;

struct RegDefault {
    uint8_t  reg;
    uint16_t value;
};

/* Cirrus Logic WM9713L Rev 4.0 Table 68 "WM9713L Register Map" (page 91),
   Default column. */
constexpr RegDefault kDefaults[] = {
    {0x00, 0x6174}, {0x02, 0x8080}, {0x04, 0x8080}, {0x06, 0x8080}, {0x08, 0xC880},
    {0x0A, 0xE808}, {0x0C, 0xE808}, {0x0E, 0x0808}, {0x10, 0x00DA}, {0x12, 0x8000},
    {0x14, 0xD600}, {0x16, 0xAAA0}, {0x18, 0xAAA0}, {0x1A, 0xAAA0}, {0x1C, 0x0000},
    {0x1E, 0x0000}, {0x20, 0x0F0F}, {0x22, 0x0040}, {0x24, 0x0000}, {0x26, 0x7F00},
    {0x28, 0x0405}, {0x2A, 0x0410}, {0x2C, 0xBB80}, {0x2E, 0xBB80}, {0x32, 0xBB80},
    {0x36, 0x4523}, {0x3A, 0x2000}, {0x3C, 0xFDFF}, {0x3E, 0xFFFF}, {0x40, 0x0000},
    {0x42, 0x0000}, {0x44, 0x0080}, {0x46, 0x0000}, {0x4C, 0xFFFE}, {0x4E, 0xFFFF},
    {0x50, 0x0000}, {0x52, 0x0000}, {0x56, 0xFFFE}, {0x58, 0x4000}, {0x5A, 0x0000},
    {0x5C, 0x0000}, {0x60, 0xB032}, {0x62, 0x3E00}, {0x64, 0x0000}, {0x74, 0x0000},
    {0x76, 0x0006}, {0x78, 0x0001}, {0x7A, 0x0000},
    {kRegVendorId1, kWm97xxId1}, {kRegVendorId2, kWm9713Id2},
};

/* Cirrus Logic WM9713L Rev 4.0 Table 65 (page 87): register 3Ch bit 7 DACL, bit 6
   DACR, bit 5 ADCL, bit 4 ADCR, "1 = Disabled". */
constexpr uint32_t kRegPowerdown1 = 0x3Cu;
constexpr uint16_t kDacl = 1u << 7;
constexpr uint16_t kDacr = 1u << 6;
constexpr uint16_t kAdcl = 1u << 5;
constexpr uint16_t kAdcr = 1u << 4;
/* Cirrus Logic WM9713L Rev 4.0 Table 21 (page 44): 2Ch, 32h and 2Eh rates 1F40h, 2B11h, 2EE0h, 3E80h,
   5622h, 5DC0h, 7D00h, AC44h, BB80h. */
constexpr uint32_t kRegAuxDacRate = 0x2Eu;
constexpr uint16_t kRates[] = {8000u, 11025u, 12000u, 16000u, 22050u, 24000u, 32000u, 44100u, 48000u};
/* Cirrus Logic WM9713L Rev 4.0 Table 65 (page 87): 3Ch AUXDAC bit 11, "1 = Disabled". */
constexpr uint16_t kAuxdac = 1u << 11;

/* Cirrus Logic WM9713L Rev 4.0 Table 62 (page 84): 4Eh GPn polarity (1 = active high); Table 61 (page
   83): GPIO bit 13 "Internal PENDOWN Signal enabled only when pen-down detection is active". */
constexpr uint32_t kRegGpioPolarity = 0x4Eu;
constexpr uint32_t kRegGpioStatus   = 0x54u;
constexpr uint16_t kGpioPenDown     = 1u << 13;
/* Cirrus Logic WM9713L Rev 4.0 Table 62 (page 84): 50h GSn "1 = Sticky", 52h GWn "1 = Wake-up (generate
   interrupts from GPIO)"; Figure 35 (page 83): "WAKEEN 5Ah, bit 1". */
constexpr uint32_t kRegGpioSticky = 0x50u;
constexpr uint32_t kRegGpioWake   = 0x52u;
constexpr uint16_t kWakeen        = 1u << 1;

/* Cirrus Logic WM9713L Rev 4.0 Table 7 (page 25): 26h PR5 bit 13, 3Ch PLL bit 9, "both PR5 and PLL must be
   asserted low before PLL is enabled". */
constexpr uint32_t kRegClockCtrl = 0x44u;
constexpr uint32_t kRegPll       = 0x46u;
constexpr uint16_t kPr5          = 1u << 13;
constexpr uint16_t kPllOff       = 1u << 9;
/* Cirrus Logic WM9713L Rev 4.0 page 121 (index 78h): WAIT 9 "1 = Retain existing data in 7Ah until it is
   read", MSK 7:6; Table 56 (page 77): 11 "conversions wait until rising or falling edge on MASK". */
constexpr uint16_t kWait       = 1u << 9;
constexpr uint32_t kMskShift   = 6u;
constexpr uint32_t kMskSync    = 3u;
/* Cirrus Logic WM9713L Rev 4.0 Table 10 (page 32): 5Ch ASS 1:0 left data on slot 3, 7, 6 or 10. */
constexpr uint32_t kRegAddFunc2 = 0x5Cu;
constexpr uint32_t kAssLeftSlot[4] = {3u, 7u, 6u, 10u};
/* Cirrus Logic WM9713L Rev 4.0 Table 68 (page 91): 40h LB 7; 2Ah SPSA 5:4, SEN 2; 5Ch ADCO 4. */
constexpr uint32_t kRegGeneral = 0x40u;
constexpr uint16_t kLb         = 1u << 7;
constexpr uint32_t kSpsaShift  = 4u;
constexpr uint16_t kSen        = 1u << 2;
constexpr uint16_t kAdco       = 1u << 4;

}  // namespace

bool Wm9713Codec::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SymbolHamptons;
}

void Wm9713Codec::OnReady() {
    Wm97xxCodec::OnReady();
    clocks_ = &emu_.Get<Wm9713Clocks>();
    ColdReset();
}

void Wm9713Codec::ColdReset() {
    ResetRegisters();
    gpio_int_ = false;
    digitiser_.Clear(pen_down_);
}

void Wm9713Codec::LinkStarted(uint64_t frame) {
    Wm97xxCodec::LinkStarted(frame);
    EvaluateGpio(frame + 1u, emu_.Get<GuestCycleClock>().Cycles());
}

void Wm9713Codec::ResetRegisters() {
    stalled_poll_ = false;
    pen_q_        = false;
    for (uint16_t& r : reg_) r = 0u;
    for (const RegDefault& d : kDefaults) reg_[d.reg] = d.value;
    clocks_->Reset();
    /* symbol_mk500 touch.dll FUN_022381a0 @0x022381a0 applies the reg 0x5C and
       reg 0x68 errata writes only for revision index 0. */
    reg_[kReg5A] = kRevisionC;
    ResetRates();
}

const uint16_t* Wm9713Codec::SupportedRates(uint32_t& count) const {
    count = static_cast<uint32_t>(sizeof(kRates) / sizeof(kRates[0]));
    return kRates;
}

bool Wm9713Codec::IsRateRegister(uint32_t reg) const {
    return reg == kRegAuxDacRate || Ac97Codec::IsRateRegister(reg);
}

/* Cirrus Logic WM9713L Rev 4.0 Table 21 note (page 44): a rate change "will only be effective if the ADCs and
   DACs are enabled and powered up before the sample rate is changed". */
bool Wm9713Codec::RateWriteTakesEffect(uint32_t reg) {
    if (reg == kRegDacRate) return DacPowered();
    if (reg == kRegAdcRate) return AdcPowered();
    return (reg_[kRegPowerdown] & kPr3) == 0u && (reg_[kRegPowerdown1] & kAuxdac) == 0u;
}

void Wm9713Codec::RequireRegister(uint32_t reg) {
    if (reg >= kNumRegs) emu_.Get<Fatal>().Die("Wm9713Codec: codec register index 0x%X out of range", reg);
}

uint16_t Wm9713Codec::Peek(uint32_t reg) {
    RequireRegister(reg);
    return reg_[reg];
}

void Wm9713Codec::Poke(uint32_t reg, uint16_t value) {
    RequireRegister(reg);
    reg_[reg] = value;
}

/* WM9713L Rev 4.0 page 86: a block "is active when both the relevant bit in register 26h AND
   the relevant bit in the Powerdown registers 3Ch and 3Eh are set to '0'"; Table 64 PR3
   "disables VREF, input PGAs, DACs, ADCs, mixers and outputs". */
bool Wm9713Codec::PrClear(uint16_t pr_bit) const {
    return (reg_[kRegPowerdown] & (pr_bit | kPr3)) == 0u;
}

bool Wm9713Codec::DacPowered() {
    const uint16_t pd = reg_[kRegPowerdown1];
    return PrClear(kPr1) && (pd & (kDacl | kDacr)) != (kDacl | kDacr);
}

/* Cirrus Logic WM9713L Rev 4.0 Figure 9 (page 19): "40h:7 (Loopback)" selects "AC'97 Link" or "ADC Left" for
   DACL and "ADC Right" for DACR. */
uint32_t Wm9713Codec::PcmOutChannels() {
    if (!PrClear(kPr1) || (reg_[kRegGeneral] & kLb) != 0u) return 0u;
    const uint16_t pd = reg_[kRegPowerdown1];
    return ((pd & kDacl) == 0u ? kDacLeft : 0u) | ((pd & kDacr) == 0u ? kDacRight : 0u);
}

/* Cirrus Logic WM9713L Rev 4.0 page 64: SPSA "00 = Slots 3 and 4", "This control is only valid when ADCO=0 in
   5Ch"; 3Ah SPSR "the WM9713L only supports 48kHz sampling on the S/PDIF output". */
bool Wm9713Codec::PcmOutEveryFrame() {
    const uint16_t ext   = reg_[kRegExtAudioCtrl];
    const bool     spdif = (ext & kSen) != 0u && ((ext >> kSpsaShift) & 3u) == 0u &&
                           (reg_[kRegAddFunc2] & kAdco) == 0u;
    return spdif || PcmOutChannels() == 0u;
}

/* WM9713L Rev 4.0 page 32: "If only one ADC is running, the same ADC data appears on both the left
   and right AC-Link slots". */
bool Wm9713Codec::AdcPowered() {
    const bool on = (reg_[kRegPowerdown1] & (kAdcl | kAdcr)) != (kAdcl | kAdcr);
    return PrClear(kPr0) && on;
}

GuestCycleClock::Rate Wm9713Codec::BitClockRate() {
    const bool pll_on = (reg_[kRegPowerdown] & kPr5) == 0u && (reg_[kRegPowerdown1] & kPllOff) == 0u;
    return clocks_->BitClock(reg_[kRegClockCtrl], reg_[kRegPll], pll_on);
}

uint32_t Wm9713Codec::ConverterDivider() { return clocks_->HifiDivider(reg_[kRegClockCtrl]); }

uint32_t Wm9713Codec::AdcLeftSlot() { return kAssLeftSlot[reg_[kRegAddFunc2] & 3u]; }

/* Cirrus Logic WM9713L Rev 4.0 Figure 35 (page 83): a GPIO status bit is the signal XNOR its polarity
   4Eh.n, or the RSFF latch of it with STICKY 50h.n set; Table 61 (page 83): the PENDOWN signal is
   "enabled only when pen-down detection is active". */
uint16_t Wm9713Codec::GpioSlotStatus(uint64_t frame) {
    const bool sticky = (reg_[kRegGpioSticky] & kGpioPenDown) != 0u;
    const bool status = sticky ? pen_q_ && frame >= pen_q_frame_ : PenXnor(frame);
    return status ? kGpioPenDown : 0u;
}

bool Wm9713Codec::PenXnor(uint64_t frame) const {
    if ((reg_[kRegDigitiserPower] >> 14) == 0u) return false;
    const bool active_high = (reg_[kRegGpioPolarity] & kGpioPenDown) != 0u;
    return digitiser_.PenDownAt(frame) == active_high;
}

/* Cirrus Logic WM9713L Rev 4.0 Figure 35 (page 83): GPIO_INT (slot 12 bit 0) = WAKEEN AND the OR over the
   GPIOs of (latch AND STICKY 50h.n AND WAKE-UP 52h.n); "IF WM9713L SLEEPING: WAKE UP AC-LINK"; Table 62
   (page 84) 54h "Write = Writing 0 clears sticky bits". */
void Wm9713Codec::EvaluateGpio(uint64_t frame, uint64_t now) {
    if (!pen_q_ && PenXnor(frame)) {
        pen_q_       = true;
        pen_q_frame_ = frame;
    }
    const bool level = (reg_[kReg5A] & kWakeen) != 0u && pen_q_ && (reg_[kRegGpioSticky] & kGpioPenDown) != 0u &&
                       (reg_[kRegGpioWake] & kGpioPenDown) != 0u;
    if (level == gpio_int_) return;
    gpio_int_ = level;
    if (level && LinkPoweredDown()) frames_->CodecWake(now, false);
    frames_->CodecGpioInterrupt(frame, level);
}

void Wm9713Codec::OnPenSignal(uint64_t frame, uint64_t now) { EvaluateGpio(frame, now); }

uint16_t Wm9713Codec::ReadReg(uint32_t reg, uint64_t frame) {
    switch (reg) {
    case kRegGpioStatus:  return GpioSlotStatus(frame);
    case kRegDigitiser1:
        return static_cast<uint16_t>((reg_[reg] & ~kPoll) |
                                     (stalled_poll_ || digitiser_.Polling(frame) ? kPoll : 0u));
    case kRegDigitiserRd: return ReadResultRegister(frame, reg_[kRegDigitiserRd]);
    default:              return Peek(reg);
    }
}

void Wm9713Codec::WriteReg(uint32_t reg, uint16_t value, uint64_t frame) {
    RequireRegister(reg);
    /* WM9713L Rev 4.0 page 92: "Writing any value to this register resets all registers to
       their default, but does not change the contents of reg. 00h." */
    if (reg == kRegReset) {
        ResetRegisters();
        Reconfigure(frame, false);
        EvaluateGpio(frame + 1u, emu_.Get<GuestCycleClock>().Cycles());
        return;
    }
    /* Cirrus Logic WM9713L Rev 4.0 page 122: "Registers 7Ah is a read-only register". */
    if (reg == kRegDigitiserRd) return;
    if (WriteRateRegister(reg, value)) return;
    reg_[reg] = value;
    if (reg == kRegPll) clocks_->WritePager(value);
    if (reg == kRegGpioStatus && (value & kGpioPenDown) == 0u) pen_q_ = false;
    if (reg == kRegExtAudioCtrl) OnExtAudioCtrlWrite();
    if (reg == kRegDigitiser1) {
        stalled_poll_ = false;
        LOG(Periph, "[WM9713] DIG1=0x%04X sel=0x%02X%s%s\n", value, value & kSelMask,
            (value & kCtc) ? " CTC" : "", (value & kPoll) ? " POLL" : "");
    }
    if (reg == kRegDigitiser1 || reg == kRegDigitiser2 || reg == kRegDigitiserPower || reg == kRegPowerdown1) {
        Reconfigure(frame, reg == kRegDigitiser1 && (value & kPoll) != 0u);
    }
    if (reg == kRegGpioPolarity || reg == kRegGpioSticky || reg == kRegGpioWake || reg == kRegGpioStatus ||
        reg == kReg5A || reg == kRegDigitiserPower) {
        EvaluateGpio(frame + 1u, emu_.Get<GuestCycleClock>().Cycles());
    }
}

/* Cirrus Logic WM9713L Rev 4.0 page 72: in polling-coordinate mode "an X co-ordinate, then a Y
   co-ordinate, followed by a single additional measurement determined by ADCSEL[7:1], then stops"; in
   continuous-coordinate mode the additional measurement "alternates for every set of three". */
void Wm9713Codec::AddCoordinateSet(Wm97xxDigitiser::Sequence& seq, uint8_t extra) {
    seq.Add(kSrcX, kResultFrames);
    seq.Add(kSrcY, kResultFrames);
    if (extra != 0u) seq.Add(extra, FramesFor(extra == kSrcPressure));
}

/* Cirrus Logic WM9713L Rev 4.0 Table 48 (page 71) PDEN: "when CTC=1, measurements are stopped on
   pen-up"; page 72: "If CTC=0 (polling mode) then only one of the ADCSEL[7:1] bits should be set",
   and in continuous mode the selected conversions run "in the following order: X, Y, PRESSURE". */
void Wm9713Codec::Reconfigure(uint64_t frame, bool poll) {
    const uint16_t d74 = reg_[kRegDigitiser1];
    const uint16_t d76 = reg_[kRegDigitiser2];
    const uint16_t d78 = reg_[kRegDigitiserPower];
    const bool powered = DigitiserPowered() && (reg_[kRegPowerdown1] & kPadcpd) == 0u;
    const bool runs    = powered && ((d78 >> kMskShift) & 3u) != kMskSync;
    const uint32_t del = (d76 >> 4) & 0xFu;
    /* Cirrus Logic WM9713L Rev 4.0 page 76: with DEL 1111 "the WM9713L behaves as if COO = 0". */
    const bool coo = (d74 & kCoo) != 0u && del != kDelNone;
    uint8_t  selected[7] = {};
    uint32_t n = 0u;
    for (uint8_t b = 1u; b <= 7u; ++b) {
        if ((d74 & kSelMask & (1u << b)) != 0u) selected[n++] = b;
    }
    Wm97xxDigitiser::Sequence seq;
    seq.slot = RouteSlot(d76);
    /* Cirrus Logic WM9713L Rev 4.0 page 74: "the WM9713L can delay overwriting register 7Ah with new
       conversions until the old data has been read"; page 77: ADA stays high until "sent out on an AC-Link
       slot (if SLEN = 1)". */
    seq.hold = (d78 & kWait) != 0u && !SlotMode(d76);
    /* Cirrus Logic WM9713L Rev 4.0 page 70: POLL "automatically resets itself when the measurement is
       completed"; Table 47: PRP 01 "Pen digitiser powered off". */
    if (poll || (stalled_poll_ && runs)) {
        if (coo) {
            AddCoordinateSet(seq, n != 0u ? selected[0] : 0u);
        } else if (n != 0u) {
            seq.Add(selected[0], FramesFor(selected[0] == kSrcPressure));
        }
        seq.group     = seq.count;
        stalled_poll_ = !runs && seq.count != 0u;
        if (!runs || seq.count == 0u) {
            digitiser_.SetContinuous(frame, nullptr);
            return;
        }
        digitiser_.StartPolled(frame, seq);
        return;
    }
    if (coo && n == 0u) {
        AddCoordinateSet(seq, 0u);
    } else if (coo) {
        for (uint32_t i = 0u; i < n; ++i) AddCoordinateSet(seq, selected[i]);
    } else {
        for (uint32_t i = 0u; i < n; ++i) seq.Add(selected[i], FramesFor(selected[i] == kSrcPressure));
    }
    seq.group = coo && n != 0u ? 3u : seq.count;
    if ((d74 & kCtc) == 0u || !runs || seq.count == 0u) {
        digitiser_.SetContinuous(frame, nullptr);
        return;
    }
    const uint32_t cr = (d76 >> 8) & 3u;
    seq.period    = del == kDelNone ? FastRateFrames(cr) : kCrFrames[cr];
    seq.pen_gated = (d78 & kPden) != 0u;
    digitiser_.SetContinuous(frame, &seq);
}

uint16_t Wm9713Codec::ConversionData(uint8_t tag) {
    switch (tag) {
    case kSrcX:        return RawX();
    case kSrcY:        return RawY();
    default:
        emu_.Get<Fatal>().Die("Wm9713Codec: conversion of ADC source %u; not modelled", tag);
    }
}

void Wm9713Codec::SaveState(StateWriter& w) {
    w.WriteBytes("reg", reg_, sizeof(reg_));
    w.Write<uint8_t>("stalled_poll", stalled_poll_ ? 1u : 0u);
    w.Write<uint8_t>("gpio_pen_q", pen_q_ ? 1u : 0u);
    w.Write<uint64_t>("gpio_pen_q_frame", pen_q_frame_);
    w.Write<uint8_t>("gpio_int", gpio_int_ ? 1u : 0u);
    clocks_->Save(w);
    SaveRates(w);
    SavePen(w);
}

void Wm9713Codec::RestoreState(StateReader& r) {
    uint8_t stalled = 0, pen_q = 0, gpio_int = 0;
    r.ReadBytes("reg", reg_, sizeof(reg_));
    r.Read("stalled_poll", stalled);
    r.Read("gpio_pen_q", pen_q);
    r.Read("gpio_pen_q_frame", pen_q_frame_);
    r.Read("gpio_int", gpio_int);
    stalled_poll_ = stalled != 0u;
    pen_q_        = pen_q != 0u;
    gpio_int_     = gpio_int != 0u;
    clocks_->Restore(r);
    RestoreRates(r);
    RestorePen(r);
}

REGISTER_SERVICE_AS(Wm9713Codec, Ac97Codec);

}
