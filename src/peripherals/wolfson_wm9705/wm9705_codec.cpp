#include "wm9705_codec.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/askey_pc3xx/askey_pc3xx_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace {
/* falcon_4220__4_10 touch.dll sub_18E1CE8 requires register 7Ch = 574Dh and 7Eh = 4C05h (WM9705) or
   4C12h before it enables the touch screen. */
constexpr uint32_t kRegVendorId1 = 0x7Cu;
constexpr uint32_t kRegVendorId2 = 0x7Eu;
constexpr uint16_t kWm97xxId1    = 0x574Du;
constexpr uint16_t kWm9705Id2    = 0x4C05u;

/* Wolfson WM9705 page 41 (index 76h): POLL 15, ADR 14:12, COO 11, CTC 10, CR 9:8, DEL 7:4, SLEN 3, SLT
   2:0; page 42 (index 78h): PRP 15:14 (11 = powered up, 01 = wakeup on pen down), PDEN 12. */
constexpr uint32_t kRegDigitiser1  = 0x76u;
constexpr uint32_t kRegDigitiserRd = 0x7Au;
constexpr uint16_t kPoll = 1u << 15, kCoo = 1u << 11, kCtc = 1u << 10;
constexpr uint16_t kPden = 1u << 12;
/* Wolfson WM9705 page 42 (index 78h): WAIT 8, MSK[1:0] 5:4, "11 = Mask pin becomes a synchronise input;
   conversions happen after edge". */
constexpr uint16_t kWait     = 1u << 8;
constexpr uint32_t kMskShift = 4u;
constexpr uint32_t kMskSync  = 3u;
/* Wolfson WM9705 Table 22 (page 38): 5Ch ASS1, ASS0 slots 3 and 4, 7 and 8, 6 and 9, 10 and 11. */
constexpr uint32_t kRegAddFunc     = 0x5Cu;
constexpr uint32_t kAssLeftSlot[4] = {3u, 7u, 6u, 10u};
/* Wolfson WM9705 Table 23 (page 39): 20h LPBK D7; 28h DSA1 D5, DSA0 D4; 2Ah SPSA1 D5, SPSA0 D4, SPDIF D2;
   5Ch I2S D6, ADCO D4. */
constexpr uint32_t kRegGeneral    = 0x20u;
constexpr uint32_t kRegExtAudioId = 0x28u;
constexpr uint16_t kLpbk          = 1u << 7;
constexpr uint32_t kSlotPairShift = 4u;
constexpr uint16_t kSpdif         = 1u << 2;
constexpr uint16_t kI2s           = 1u << 6;
constexpr uint16_t kAdco          = 1u << 4;
/* Wolfson WM9705 Table 24 (page 43): ADR 001 X-plate, 010 Y-plate, 011 pressure, 101 AUXADC; Table 26
   (page 46): CR 00 512, 01 256, 10 128, 11 64 AC link frames. */
constexpr uint8_t  kAdrX = 1u, kAdrY = 2u, kAdrPressure = 3u, kAdrAuxAdc = 5u;
constexpr uint32_t kCrFrames[4] = {512u, 256u, 128u, 64u};

/* falcon_4220__4_10 battdrvr.dll sub_17F1C9C converts this AUXADC result to 3300 * adc / 4096 mV;
   sub_17F1668 reports the backup battery from it, 100 percent at 3100 mV and above. */
constexpr uint16_t kAuxAdcValue = 0x0FFFu;

/* Wolfson WM9705 Table 23 note 2 (page 39): "Register 5Ah is write only. When writing to this register
   all bits except MPM (bit 4) must be written as 0"; falcon_4220__4_10 touch.dll sub_18E1CE8 sets 78h
   |= C000h only when its 5Ah read & 0x60 is 0x20. */
constexpr uint32_t kRegMixerMute  = 0x5Au;
constexpr uint16_t kMixerMuteRead = 0x0020u;
constexpr uint16_t kMixerMuteMpm  = 1u << 4;

struct RegDefault {
    uint8_t  reg;
    uint16_t value;
};

/* Wolfson WM9705 Production Data Rev 4.5 Table 23 "Serial Interface Register
   Map Description" (page 39), Default column. */
constexpr RegDefault kDefaults[] = {
    {0x00, 0x6150}, {0x02, 0x8000}, {0x04, 0x8000}, {0x06, 0x8000}, {0x0A, 0x8000},
    {0x0C, 0x8008}, {0x0E, 0x8008}, {0x10, 0x8808}, {0x12, 0x8808}, {0x14, 0x8808},
    {0x16, 0x8808}, {0x18, 0x8808}, {0x1A, 0x0000}, {0x1C, 0x8000}, {0x20, 0x0000},
    {0x22, 0x0000}, {0x26, 0x000F}, {0x28, 0x0605}, {0x2A, 0x0000}, {0x2C, 0xBB80},
    {0x32, 0xBB80}, {0x3A, 0x2000}, {0x5A, 0x0000}, {0x5C, 0x0000}, {0x72, 0x0808},
    {0x74, 0x0000}, {0x76, 0x0006}, {0x78, 0x0000}, {0x7A, 0x0000},
    {kRegVendorId1, kWm97xxId1}, {kRegVendorId2, kWm9705Id2},
};

/* Wolfson WM9705 page 35: "only Revision 2.2 recommended rates are supported"; AC '97 Component
   Specification Revision 2.2 section 1.6 (page 14): "8.0, 11.025, 16.0, 22.05, 32.0, 44.1, and 48 kHz". */
constexpr uint16_t kRates[] = {8000u, 11025u, 16000u, 22050u, 32000u, 44100u, 48000u};
}  // namespace

bool Wm9705Codec::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::AskeyPc3xx;
}

void Wm9705Codec::OnReady() {
    Wm97xxCodec::OnReady();
    ColdReset();
}

void Wm9705Codec::LoadDefaults() {
    for (uint16_t& r : reg_) r = 0u;
    for (const RegDefault& d : kDefaults) reg_[d.reg] = d.value;
    stalled_poll_ = false;
    ResetRates();
}

const uint16_t* Wm9705Codec::SupportedRates(uint32_t& count) const {
    count = static_cast<uint32_t>(sizeof(kRates) / sizeof(kRates[0]));
    return kRates;
}

void Wm9705Codec::ColdReset() {
    LoadDefaults();
    digitiser_.Clear(pen_down_);
}

void Wm9705Codec::RequireRegister(uint32_t reg) {
    if (reg >= kNumRegs) emu_.Get<Fatal>().Die("Wm9705Codec: codec register index 0x%X out of range", reg);
}

uint16_t Wm9705Codec::Peek(uint32_t reg) {
    RequireRegister(reg);
    return reg_[reg];
}

void Wm9705Codec::Poke(uint32_t reg, uint16_t value) {
    RequireRegister(reg);
    reg_[reg] = value;
}

uint16_t Wm9705Codec::ReadReg(uint32_t reg, uint64_t frame) {
    switch (reg) {
    case kRegMixerMute:   return kMixerMuteRead;
    case kRegDigitiser1:
        return static_cast<uint16_t>((reg_[reg] & ~kPoll) |
                                     (stalled_poll_ || digitiser_.Polling(frame) ? kPoll : 0u));
    case kRegDigitiserRd: return ReadResultRegister(frame, reg_[kRegDigitiserRd]);
    default:              return Peek(reg);
    }
}

uint32_t Wm9705Codec::AdcLeftSlot() { return kAssLeftSlot[reg_[kRegAddFunc] & 3u]; }

/* AC '97 Component Specification Revision 2.1 section 6.3.1 (page 39): "Writing any
   value to this register performs a register reset, which causes all registers to
   revert to their default values." */
void Wm9705Codec::WriteReg(uint32_t reg, uint16_t value, uint64_t frame) {
    if (reg == kRegReset) {
        LoadDefaults();
        Reconfigure(frame, false);
        return;
    }
    /* Wolfson WM9705 page 36: with MPM set, "DAC + PHONE + PCBEEP to line out / headphone out"; "When
       writing to this register all bits (except MPM) must be written as a 0". */
    if (reg == kRegMixerMute) {
        if ((value & ~kMixerMuteMpm) != 0u) {
            emu_.Get<Fatal>().Die("Wm9705Codec: register 5Ah write 0x%04X; not modelled", value);
        }
        return;
    }
    /* Wolfson WM9705 page 42: "INDEX 7AH - DIGITISER DATA WORD OUTPUT - READ REGISTER ONLY". */
    if (reg == kRegDigitiserRd) return;
    if (WriteRateRegister(reg, value)) return;
    Poke(reg, value);
    if (reg == kRegDigitiser1) stalled_poll_ = false;
    if (reg == kRegExtAudioCtrl) OnExtAudioCtrlWrite();
    if (reg == kRegDigitiser1 || reg == kRegDigitiserPower) {
        Reconfigure(frame, reg == kRegDigitiser1 && (value & kPoll) != 0u);
    }
}

/* Wolfson WM9705 page 51: "Setting PRP[1:0] to 00 will power off the digitiser and pen down
   detection". */
void Wm9705Codec::Reconfigure(uint64_t frame, bool poll) {
    const uint16_t d1 = reg_[kRegDigitiser1];
    const uint16_t d2 = reg_[kRegDigitiserPower];
    const bool     powered = DigitiserPowered();
    const uint8_t  adr     = static_cast<uint8_t>((d1 >> 12) & 7u);
    const uint32_t del     = (d1 >> 4) & 0xFu;
    const bool     sync    = ((d2 >> kMskShift) & 3u) == kMskSync;
    const bool     polled  = poll || stalled_poll_;
    Wm97xxDigitiser::Sequence seq;
    seq.slot = RouteSlot(d1);
    /* Wolfson WM9705 page 43: with WAIT set "the current conversion result may not be written into the results
       register, until the previous conversion result has been read". */
    seq.hold = (d2 & kWait) != 0u && !SlotMode(d1);
    /* Wolfson WM9705 page 46: "So long as the ADR[2-0] value remains set to auxiliary or pressure inputs,
       X,Y and the extra auxiliary conversions will be made in every cycle"; Figure 22 (page 45). */
    if ((d1 & kCoo) != 0u) {
        seq.Add(kAdrX, kResultFrames);
        seq.Add(kAdrY, kResultFrames);
        if (adr >= kAdrPressure) seq.Add(adr, FramesFor(adr == kAdrPressure));
    } else if (polled || adr != 0u) {
        seq.Add(adr, FramesFor(adr == kAdrPressure));
    }
    seq.group = seq.count;
    if (polled) {
        stalled_poll_ = sync;
        if (sync) {
            digitiser_.SetContinuous(frame, nullptr);
            return;
        }
        digitiser_.StartPolled(frame, seq);
        return;
    }
    if ((d1 & kCtc) == 0u || !powered || sync || seq.count == 0u) {
        digitiser_.SetContinuous(frame, nullptr);
        return;
    }
    const uint32_t cr = (d1 >> 8) & 3u;
    seq.period    = del == kDelNone ? FastRateFrames(cr) : kCrFrames[cr];
    seq.pen_gated = (d2 & kPden) != 0u;
    digitiser_.SetContinuous(frame, &seq);
}

uint16_t Wm9705Codec::ConversionData(uint8_t tag) {
    switch (tag) {
    case kAdrX:        return RawX();
    case kAdrY:        return RawY();
    case kAdrAuxAdc:   return kAuxAdcValue;
    default:
        emu_.Get<Fatal>().Die("Wm9705Codec: conversion of ADR channel %u; not modelled", tag);
    }
}

/* Wolfson WM9705 Table 15 "Powerdown Control Register Function" (page 33): "PR0
   PCM in ADCs and input Mux Powerdown", "PR1 PCM out DACs Powerdown"; "PR0 and
   PR1 control the PCM ADCs and DACs only." */
bool Wm9705Codec::DacPowered() { return (reg_[kRegPowerdown] & kPr1) == 0u; }

bool Wm9705Codec::AdcPowered() { return (reg_[kRegPowerdown] & kPr0) == 0u; }

/* Wolfson WM9705 Table 17 (page 35): DSA 00 "Slots 3 and 4"; page 32: "The LPBK bit enables loopback of the
   ADC output to the DAC input without involving the AC-link". */
uint32_t Wm9705Codec::PcmOutChannels() {
    const bool on_slots = ((reg_[kRegExtAudioId] >> kSlotPairShift) & 3u) == 0u && (reg_[kRegGeneral] & kLpbk) == 0u;
    return DacPowered() && on_slots ? kDacLeft | kDacRight : 0u;
}

/* Wolfson WM9705 page 20: the SPDIF / I2S output slots "are selected by bits SPSA[1:0] in register 2Ah"; page 21:
   "WM9705 supports SPDIF and I2S data only at the default 48ks/s frame rate", ADCO sends the ADC output instead. */
bool Wm9705Codec::PcmOutEveryFrame() {
    const uint16_t ext     = reg_[kRegExtAudioCtrl];
    const uint16_t add     = reg_[kRegAddFunc];
    const bool     digital = ((ext & kSpdif) != 0u || (add & kI2s) != 0u) && (add & kAdco) == 0u &&
                             ((ext >> kSlotPairShift) & 3u) == 0u;
    return digital || PcmOutChannels() == 0u;
}

void Wm9705Codec::SaveState(StateWriter& w) {
    w.WriteBytes("reg", reg_, sizeof(reg_));
    w.Write<uint8_t>("stalled_poll", stalled_poll_ ? 1u : 0u);
    SaveRates(w);
    SavePen(w);
}

void Wm9705Codec::RestoreState(StateReader& r) {
    uint8_t stalled = 0;
    r.ReadBytes("reg", reg_, sizeof(reg_));
    r.Read("stalled_poll", stalled);
    stalled_poll_ = stalled != 0u;
    RestoreRates(r);
    RestorePen(r);
}

REGISTER_SERVICE_AS(Wm9705Codec, Ac97Codec);
