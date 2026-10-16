#include "tps65950_audio_downlink.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ti_omap35x_evm/ti_omap35x_evm_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "tps65950_gain.h"
#include "twl4030.h"

namespace {

/* TPS65950 TRM SWCU050E Table 14-15 (page 728) CODEC_MODE OPT_MODE [0]; Table 14-16 (page 728) OPTION ARXR2_EN
   [7], ARXL2_EN [6], ARXR1_EN [5] (option 1 only), ARXL1_VRX_EN [4] (audio RX left 1 in option 1). */
constexpr uint8_t kCodecMode = 0x01u, kOption = 0x02u, kDigmixing = 0x09u, kAvdacCtl = 0x17u;
constexpr uint8_t kAtx2Arx = 0x1Du, kEarCtl = 0x21u, kHsSel = 0x22u, kHsGain = 0x23u, kHsPopn = 0x24u;
constexpr uint8_t kPredl = 0x25u, kPredr = 0x26u, kPreckl = 0x27u, kPreckr = 0x28u, kHfl = 0x29u, kHfr = 0x2Au;
constexpr uint8_t kBoostCtl = 0x2Eu, kSoftvolCtl = 0x2Fu, kMiscSet1 = 0x3Eu, kRxPathSel = 0x43u, kVdlApga = 0x44u;
constexpr uint8_t kOptMode  = 1u << 0;

using Sdr = Tps65950AudioDownlink::Sdr;
constexpr Sdr kChannels[4] = {Sdr::kL1, Sdr::kR1, Sdr::kL2, Sdr::kR2};

/* SWCU050E Table 14-30 (page 741) 0x11, Table 14-29 (page 740) 0x10, Table 14-32 (page 743) 0x13, Table 14-31
   (page 742) 0x12; Table 14-36 (page 747) ADACL1_EN [1], ADACR1_EN [0], ADACL2_EN [3], ADACR2_EN [2]; Table 14-38
   (page 749) 0x19 to Table 14-41 (page 752) 0x1C. */
constexpr uint8_t kFilterEn[4] = {1u << 4, 1u << 5, 1u << 6, 1u << 7};
constexpr uint8_t kPga[4]      = {0x11u, 0x10u, 0x13u, 0x12u};
constexpr uint8_t kDacEn[4]    = {1u << 1, 1u << 0, 1u << 3, 1u << 2};
constexpr uint8_t kApga[4]     = {0x19u, 0x1Au, 0x1Bu, 0x1Cu};
constexpr uint8_t kVdacEn      = 1u << 4;

/* SWCU050E Table 14-38 (page 749), Table 14-78 (page 780): FM_EN [2], DA_EN [1], PDZ [0]. */
constexpr uint8_t kFmEn = 1u << 2, kDaEn = 1u << 1, kPdz = 1u << 0;

/* SWCU050E Table 14-75 (page 777) SCRAMBLE_EN [6], FMLOOP_EN [5], SMOOTH_ANAVOL_EN [1]; Table 14-60 (page 769)
   SOFTVOL_EN [0]; Table 14-22 (page 733) ARX1_MIXING [7:6], ARX2_MIXING [5:4]; Table 14-59 (page 768) EFFECT. */
constexpr uint8_t kScrambleEn = 1u << 6, kFmloopEn = 1u << 5, kSmoothAnavolEn = 1u << 1, kSoftvolEn = 1u << 0;

/* SWCU050E Table 14-29 (page 740): CGAIN [7:6] 0 / 6 / 12 / 12 dB, FGAIN [5:0] 0x0 Mute, 0x1 -62 dB in 1-dB steps
   to 0x3F 0 dB. */
int64_t DigitalGainQ16(uint8_t pga) {
    static constexpr int32_t kCoarseDb[4] = {0, 6, 12, 12};
    const int32_t            fine         = pga & 0x3F;
    return fine == 0 ? 0 : tps65950::GainQ16(fine - 63 + kCoarseDb[pga >> 6]);
}

/* SWCU050E Table 14-38 (page 749): GAIN_SET [7:3] 0x0 12 dB in -2 dB steps; page 698: analog volume control from
   -24 to 12 dB in 2-dB steps. */
constexpr uint32_t kApgaCodeMax = 0x12u;

/* SWCU050E Table 14-46 (page 756) EAR_GAIN 1 12 dB, 2 6 dB, 3 0 dB; Table 14-48 (page 758) HSL_GAIN / HSR_GAIN and
   Table 14-50 (page 760) PREDL_GAIN 1 6 dB, 2 0 dB, 3 -6 dB; code 0 is the power-down mode. */
double EarGain(uint32_t code) { return tps65950::Linear(18 - 6 * static_cast<int32_t>(code)); }
double AmpGain(uint32_t code) { return tps65950::Linear(12 - 6 * static_cast<int32_t>(code)); }

/* TPS65950 datasheet SWCS032F Figure 5-16 (page 69): the hands-free path "Amp 10.4 dB"; Table 5-29 (page 70)
   note: "Output driver = 10.4 dB". */
constexpr double kHandsFreeDb = 10.4;

}

bool Tps65950AudioDownlink::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
}

void Tps65950AudioDownlink::OnReady() {
    twl_ = &emu_.Get<Twl4030>();
    Refresh();
}

bool Tps65950AudioDownlink::SdrOn(Sdr ch) const {
    const bool opt1 = (twl_->AudioReg(kCodecMode) & kOptMode) != 0u;
    if ((ch == Sdr::kL1 || ch == Sdr::kR1) && !opt1) return false;
    return (twl_->AudioReg(kOption) & kFilterEn[ch]) != 0u;
}

/* SWCU050E Table 14-77 RX_PATH_SEL (page 779): RXL2_SEL [5], RXR2_SEL [4], RXL1_SEL [3:2], RXR1_SEL [1:0];
   SDRL1 / SDRR1 and SDRM1 (mix of SDRL1 and SDRR1) only in option 1; SDRM2 mixes SDRL2 and SDRR2. */
Tps65950AudioDownlink::Signal Tps65950AudioDownlink::SdrSelection(Sdr ch) const {
    const uint8_t sel  = twl_->AudioReg(kRxPathSel);
    uint32_t      code = 0u;
    switch (ch) {
    case Sdr::kL1: code = (sel >> 2) & 3u; break;
    case Sdr::kR1: code = sel & 3u; break;
    case Sdr::kL2: code = ((sel >> 5) & 1u) != 0u ? 3u : 2u; break;
    case Sdr::kR2: code = ((sel >> 4) & 1u) != 0u ? 3u : 2u; break;
    }
    const bool left = ch == Sdr::kL1 || ch == Sdr::kL2;
    Signal s;
    auto add = [&](Sdr src, int32_t half) {
        if (SdrOn(src)) s.half[src] += half;
    };
    switch (code) {
    case 0u: add(left ? Sdr::kL1 : Sdr::kR1, 2); break;
    case 1u: add(Sdr::kL1, 1); add(Sdr::kR1, 1); break;
    case 2u: add(left ? Sdr::kL2 : Sdr::kR2, 2); break;
    default: add(Sdr::kL2, 1); add(Sdr::kR2, 1); break;
    }
    return s;
}

/* SWCU050E Table 14-36 (page 747) DAC power; Table 14-40 (page 751): DA_EN "Digital-to-analog path enable", PDZ
   power-down mode; Figure 14-6 (page 694): the digital fine and coarse gain precede the DAC, the analog PGA follows. */
Tps65950AudioDownlink::Channel Tps65950AudioDownlink::AnalogChannel(Sdr ch) const {
    const uint8_t apga = twl_->AudioReg(kApga[ch]);
    const uint8_t pga  = twl_->AudioReg(kPga[ch]);
    Channel       c;
    if ((pga & 0x3Fu) == 0u || (twl_->AudioReg(kAvdacCtl) & kDacEn[ch]) == 0u || (apga & kPdz) == 0u ||
        (apga & kDaEn) == 0u) {
        return c;
    }
    const uint32_t code = apga >> 3;
    if (code > kApgaCodeMax) {
        emu_.Get<Fatal>().Die("Tps65950Audio: APGA 0x%02X written 0x%02X selects a reserved GAIN_SET 0x%X", kApga[ch],
                              apga, code);
    }
    c.input   = SdrSelection(ch);
    c.digital = DigitalGainQ16(pga);
    c.analog  = tps65950::Linear(12 - 2 * static_cast<int32_t>(code));
    return c;
}

/* SWCU050E Table 14-36 VDAC_EN [4]; Table 14-78 VDL_APGA_CTL (page 780) VDL_FM_EN [2], VDL_DA_EN [1], VDL_PDZ [0]. */
bool Tps65950AudioDownlink::VoiceActive() const {
    const uint8_t vdl = twl_->AudioReg(kVdlApga);
    return (twl_->AudioReg(kAvdacCtl) & kVdacEn) != 0u && (vdl & kPdz) != 0u && (vdl & (kDaEn | kFmEn)) != 0u;
}

void Tps65950AudioDownlink::RequireModelled() const {
    auto&         fatal = emu_.Get<Fatal>();
    const uint8_t misc  = twl_->AudioReg(kMiscSet1);
    if ((misc & (kScrambleEn | kFmloopEn)) != 0u) {
        fatal.Die("Tps65950Audio: MISC_SET_1 0x%02X enables the data scramble or the analog FM loop; not modelled", misc);
    }
    if ((misc & kSmoothAnavolEn) != 0u) {
        fatal.Die("Tps65950Audio: MISC_SET_1 0x%02X enables the smooth analog volume transition; not modelled", misc);
    }
    if ((twl_->AudioReg(kSoftvolCtl) & kSoftvolEn) != 0u) {
        fatal.Die("Tps65950Audio: SOFTVOL_CTL 0x%02X enables the soft volume sweep; not modelled",
                  twl_->AudioReg(kSoftvolCtl));
    }
    if ((twl_->AudioReg(kBoostCtl) & 3u) != 0u) {
        fatal.Die("Tps65950Audio: BOOST_CTL 0x%02X selects a bass-boost mode; not modelled", twl_->AudioReg(kBoostCtl));
    }
    if ((twl_->AudioReg(kAtx2Arx) & 0x3Fu) != 0u) {
        fatal.Die("Tps65950Audio: ATX2ARXPGA 0x%02X opens the audio TX-to-RX loop; not modelled", twl_->AudioReg(kAtx2Arx));
    }
    if ((twl_->AudioReg(kDigmixing) & 0xF0u) != 0u) {
        fatal.Die("Tps65950Audio: DIGMIXING 0x%02X adds voice RX to the audio RX paths; the voice downlink is not "
                  "modelled", twl_->AudioReg(kDigmixing));
    }
    for (Sdr ch : kChannels) {
        const uint8_t apga = twl_->AudioReg(kApga[ch]);
        if ((apga & kPdz) != 0u && (apga & kFmEn) != 0u) {
            fatal.Die("Tps65950Audio: APGA 0x%02X written 0x%02X enables the FM loop path; not modelled", kApga[ch],
                      apga);
        }
    }
    /* SWCU050E Figure 14-17 (page 711): PRECKT Left / Right are the "Carkit-MCPC (stereo)" outputs; Figure 15-51
       (page 865): the audio outputs reach the D+ / D- or RXAF pads through the USB block's CEA and MCPC switches. */
    for (uint8_t reg : {kPreckl, kPreckr}) {
        const uint8_t v = twl_->AudioReg(reg);
        if ((v & 0x40u) != 0u && (v & 0x30u) != 0u && (v & 0x07u) != 0u) {
            fatal.Die("Tps65950Audio: PRECK 0x%02X written 0x%02X drives a carkit output; the USB carkit audio routing "
                      "is not modelled", reg, v);
        }
    }
}

void Tps65950AudioDownlink::AddOutput(Sides& out, int side, uint8_t sources, const Channel (&channel)[4],
                                      const uint8_t (&bit)[4], bool voice, double gain) const {
    if (voice && VoiceActive()) {
        emu_.Get<Fatal>().Die("Tps65950Audio: the voice downlink reaches an active output; not modelled");
    }
    for (Sdr ch : kChannels) {
        if (bit[ch] != 0u && (sources & bit[ch]) != 0u) out[side][ch] += gain * channel[ch].analog;
    }
}

/* SWCU050E Table 14-46 (page 756) to Table 14-55 (page 765): source enables, gain 0x0 = power-down mode, OUTLOW_EN
   output held low, HS VMID_EN / RAMP_EN / EXTMUTE, hands-free application mode only with REF_EN, RAMP_EN, LOOP_EN
   and HB_EN set; page 707 NOTE: "the mixing operation is a summing". */
void Tps65950AudioDownlink::Refresh() {
    RequireModelled();
    Channel channel[4];
    for (Sdr ch : kChannels) channel[ch] = AnalogChannel(ch);
    Sides out = {};

    const uint8_t ear = twl_->AudioReg(kEarCtl);
    if ((ear & 0x30u) != 0u && (ear & 0x80u) == 0u) {
        const uint8_t bits[4] = {1u << 1, 1u << 3, 1u << 2, 0u};
        AddOutput(out, 0, ear, channel, bits, (ear & 1u) != 0u, EarGain((ear >> 4) & 3u));
        AddOutput(out, 1, ear, channel, bits, (ear & 1u) != 0u, EarGain((ear >> 4) & 3u));
    }
    const uint8_t hs   = twl_->AudioReg(kHsSel);
    const uint8_t gain = twl_->AudioReg(kHsGain);
    const uint8_t popn = twl_->AudioReg(kHsPopn);
    const bool    hs_on = (hs & 0x40u) == 0u && (popn & 0x40u) != 0u && (popn & 0x02u) != 0u && (popn & 0x20u) == 0u;
    if (hs_on && (gain & 0x03u) != 0u) {
        const uint8_t bits[4] = {1u << 1, 0u, 1u << 2, 0u};
        AddOutput(out, 0, hs, channel, bits, (hs & 1u) != 0u, AmpGain(gain & 3u));
    }
    if (hs_on && (gain & 0x0Cu) != 0u) {
        const uint8_t bits[4] = {0u, 1u << 4, 0u, 1u << 5};
        const double  sign    = (hs & 0x80u) != 0u ? -1.0 : 1.0;
        AddOutput(out, 1, hs, channel, bits, (hs & (1u << 3)) != 0u, sign * AmpGain((gain >> 2) & 3u));
    }
    const uint8_t predl = twl_->AudioReg(kPredl);
    if ((predl & 0x30u) != 0u && (predl & 0x80u) == 0u) {
        const uint8_t bits[4] = {1u << 1, 0u, 1u << 2, 1u << 3};
        AddOutput(out, 0, predl, channel, bits, (predl & 1u) != 0u, AmpGain((predl >> 4) & 3u));
    }
    const uint8_t predr = twl_->AudioReg(kPredr);
    if ((predr & 0x30u) != 0u && (predr & 0x80u) == 0u) {
        const uint8_t bits[4] = {0u, 1u << 1, 1u << 3, 1u << 2};
        AddOutput(out, 1, predr, channel, bits, (predr & 1u) != 0u, AmpGain((predr >> 4) & 3u));
    }
    const uint8_t hfl = twl_->AudioReg(kHfl);
    if ((hfl & 0x3Cu) == 0x3Cu) {
        const uint8_t sel     = static_cast<uint8_t>(1u << (hfl & 3u));
        const uint8_t bits[4] = {1u << 1, 0u, 1u << 2, 1u << 3};
        AddOutput(out, 0, sel, channel, bits, (hfl & 3u) == 0u, tps65950::Linear(kHandsFreeDb));
    }
    const uint8_t hfr = twl_->AudioReg(kHfr);
    if ((hfr & 0x3Cu) == 0x3Cu) {
        const uint8_t sel     = static_cast<uint8_t>(1u << (hfr & 3u));
        const uint8_t bits[4] = {0u, 1u << 1, 1u << 3, 1u << 2};
        AddOutput(out, 1, sel, channel, bits, (hfr & 3u) == 0u, tps65950::Linear(kHandsFreeDb));
    }

    for (Sdr ch : kChannels) {
        for (Sdr src : kChannels) input_[ch][src] = channel[ch].input.half[src];
        digital_[ch] = channel[ch].digital;
        for (int side = 0; side < 2; ++side) weight_[side][ch] = tps65950::ToQ16(out[side][ch]);
    }
}

void Tps65950AudioDownlink::Mix(const int32_t (&sdr)[4], int16_t& left, int16_t& right) const {
    int32_t dac[4] = {};
    for (Sdr ch : kChannels) {
        if (digital_[ch] == 0) continue;
        int64_t half = 0;
        for (Sdr src : kChannels) half += static_cast<int64_t>(input_[ch][src]) * sdr[src];
        dac[ch] = tps65950::Clamp16((half * digital_[ch]) >> 17);
    }
    int64_t acc[2] = {0, 0};
    for (int side = 0; side < 2; ++side) {
        for (Sdr ch : kChannels) acc[side] += weight_[side][ch] * dac[ch];
    }
    left  = static_cast<int16_t>(tps65950::Clamp16(acc[0] >> 16));
    right = static_cast<int16_t>(tps65950::Clamp16(acc[1] >> 16));
}

REGISTER_SERVICE(Tps65950AudioDownlink);
