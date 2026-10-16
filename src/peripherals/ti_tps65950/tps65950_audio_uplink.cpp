#include "tps65950_audio_uplink.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ti_omap35x_evm/ti_omap35x_evm_family_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../host/audio_activity_widget.h"
#include "tps65950_gain.h"
#include "twl4030.h"

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint16_t kChannels     = 2u;
constexpr uint16_t kBits         = 16u;
constexpr size_t   kFrameBytes   = 4u;
constexpr size_t   kMicFifoBytes = 65536u;

/* TPS65950 TRM SWCU050E Table 14-15 CODEC_MODE OPT_MODE [0]; Table 14-16 OPTION ATXL1_EN [0],
   ATXR1_EN [1], ATXL2_VTXL_EN [2], ATXR2_VTXR_EN [3]; Table 14-20 AVADC_CTL ADCL_EN [3],
   AVADC_CLK_PRIORITY [2], ADCR_EN [1]. */
constexpr uint8_t kCodecMode = 0x01u, kOption = 0x02u, kAnamicl = 0x05u, kAnamicr = 0x06u;
constexpr uint8_t kAvadcCtl  = 0x07u, kAdcmicsel = 0x08u, kAlcCtl = 0x2Bu, kAnamicGain = 0x48u;
constexpr uint8_t kOptMode   = 1u << 0;
constexpr uint8_t kAdclEn    = 1u << 3, kAudioPriority = 1u << 2, kAdcrEn = 1u << 1;

/* Table 14-18 ANAMICL: MICAMPL_EN [4], CKMIC_EN [3], AUXL_EN [2], HSMIC_EN [1], MAINMIC_EN [0];
   Table 14-19 ANAMICR: MICAMPR_EN [4], AUXR_EN [2], SUBMIC_EN [0]; Table 14-21 ADCMICSEL:
   DIGMIC1_EN [3], TX2IN_SEL [2], DIGMIC0_EN [1], TX1IN_SEL [0]. */
constexpr uint8_t kMicAmpEn   = 1u << 4;
constexpr uint8_t kLeftInputs = 0x0Fu, kRightInputs = 0x05u;
constexpr uint8_t kDigmic1En  = 1u << 3, kTx2DigMic = 1u << 2, kDigmic0En = 1u << 1, kTx1DigMic = 1u << 0;

/* SWCU050E Table 14-23 (page 733) ATXL1PGA 0x0A, Table 14-24 (page 734) ATXR1PGA 0x0B; Figure 14-6 (page 694):
   the TX path fine gain 0 to 31 dB in 1-dB steps follows the A/D converters and the digital microphone switch. */
constexpr uint8_t kTxPga[4] = {0x0Au, 0x0Cu, 0x0Bu, 0x0Du};

/* SWCU050E Table 14-56 ALC_CTL (page 766): SUBMIC_ALC_EN [4], MAINMIC_ALC_EN [3]; Table 14-81 ANAMIC_GAIN
   (page 781): MICAMPR_GAIN [5:3], MICAMPL_GAIN [2:0], 0x0 to 0x5 = 0 to 30 dB in 6-dB steps. */
constexpr uint8_t kSubmicAlcEn = 1u << 4, kMainmicAlcEn = 1u << 3;
constexpr uint8_t kMainmicEn   = 1u << 0, kSubmicEn = 1u << 0, kCkmicEn = 1u << 3;
constexpr uint8_t kMicGainMax  = 5u;

bool IsLeft(Tps65950AudioUplink::Channel ch) {
    return ch == Tps65950AudioUplink::Channel::Left1 || ch == Tps65950AudioUplink::Channel::Left2;
}

}

bool Tps65950AudioUplink::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
}

void Tps65950AudioUplink::OnReady() {
    twl_ = &emu_.Get<Twl4030>();
    mic_.Start("Tps65950Capture", kMicFifoBytes);
    Refresh();
}

void Tps65950AudioUplink::OnShutdown() { mic_.Stop(); }

void Tps65950AudioUplink::Start(uint32_t fs_hz) {
    fs_hz_ = fs_hz;
    have_  = false;
}

void Tps65950AudioUplink::Stop() {
    fs_hz_ = 0u;
    have_  = false;
    mic_.Clear();
}

/* SWCU050E Table 14-11 (page 693): with ATXx1_EN and VTXx_EN enabled and AVADC_CLK_PRIORITY voice, the
   analog microphones go to the voice path; ADCMICSEL TX1IN_SEL / TX2IN_SEL route ADCL / ADCR to TXL1 / TXR1
   and TXL2 / TXR2, or digital microphone 0 / 1 (page 732); Table 14-10: TX audio path 2 is option 1 only. */
bool Tps65950AudioUplink::Carries(Channel ch, bool& from_adc) const {
    const uint8_t mode   = twl_->AudioReg(kCodecMode);
    const uint8_t option = twl_->AudioReg(kOption);
    const uint8_t avadc  = twl_->AudioReg(kAvadcCtl);
    const uint8_t sel    = twl_->AudioReg(kAdcmicsel);
    const bool    opt1   = (mode & kOptMode) != 0u;
    const bool    path1  = ch == Channel::Left1 || ch == Channel::Right1;
    const bool    left   = IsLeft(ch);
    from_adc             = false;
    if (!path1 && !opt1) return false;
    const uint8_t enable = ch == Channel::Left1 ? 0x01u : ch == Channel::Right1 ? 0x02u
                         : ch == Channel::Left2 ? 0x04u : 0x08u;
    if ((option & enable) == 0u) return false;
    if ((sel & (path1 ? kTx1DigMic : kTx2DigMic)) != 0u) {
        return (sel & (path1 ? kDigmic0En : kDigmic1En)) != 0u;
    }
    const uint8_t voice = left ? 0x04u : 0x08u;
    if (!opt1 && (option & voice) != 0u && (avadc & kAudioPriority) == 0u) return false;
    const uint8_t mic = twl_->AudioReg(left ? kAnamicl : kAnamicr);
    from_adc          = (avadc & (left ? kAdclEn : kAdcrEn)) != 0u && (mic & kMicAmpEn) != 0u &&
                        (mic & (left ? kLeftInputs : kRightInputs)) != 0u;
    return from_adc;
}

void Tps65950AudioUplink::Refresh() {
    const uint8_t alc     = twl_->AudioReg(kAlcCtl);
    const uint8_t mic_amp = twl_->AudioReg(kAnamicGain);
    auto&         fatal   = emu_.Get<Fatal>();
    for (uint8_t i = 0; i < 4u; ++i) {
        const Channel ch   = static_cast<Channel>(i);
        bool          adc  = false;
        Path&         path = path_[i];
        path.carries       = Carries(ch, adc);
        path.digital       = tps65950::GainQ16(twl_->AudioReg(kTxPga[i]) & 0x1Fu);
        path.analog        = tps65950::GainQ16(0);
        if (!path.carries || !adc) continue;
        const bool    left = IsLeft(ch);
        const uint8_t mic  = twl_->AudioReg(left ? kAnamicl : kAnamicr);
        if ((left && (mic & kMainmicEn) != 0u && (alc & kMainmicAlcEn) != 0u) ||
            (!left && (mic & kSubmicEn) != 0u && (alc & kSubmicAlcEn) != 0u)) {
            fatal.Die("Tps65950Audio: ALC_CTL 0x%02X enables the automatic level control; not modelled", alc);
        }
        /* TPS65950 datasheet SWCS032F section 5.3.2.8 (page 94): the USB-CEA carkit input is the DP pad, the MCPC
           carkit input the TXAF pad; TRM SWCU050E Figure 15-51 (page 865): the USB block switches them by MICEN / VOICESW. */
        if (left && (mic & kCkmicEn) != 0u) {
            fatal.Die("Tps65950Audio: ANAMICL 0x%02X selects the carkit microphone input; the USB carkit audio routing "
                      "is not modelled", mic);
        }
        const uint8_t code = left ? mic_amp & 7u : (mic_amp >> 3) & 7u;
        if (code > kMicGainMax) {
            fatal.Die("Tps65950Audio: ANAMIC_GAIN 0x%02X selects reserved microphone amplifier gain %u", mic_amp,
                      code);
        }
        path.analog = tps65950::GainQ16(6 * code);
    }
}

void Tps65950AudioUplink::Advance(uint64_t frame) {
    if (fs_hz_ == 0u) {
        left_ = right_ = 0;
        have_ = false;
        return;
    }
    mic_.Request(fs_hz_, kChannels, kBits);
    uint8_t scratch[1024];
    if (have_ && frame > frame_) {
        size_t skip = static_cast<size_t>(std::min<uint64_t>(frame - frame_ - 1u, kMicFifoBytes / kFrameBytes)) *
                      kFrameBytes;
        while (skip != 0u) {
            const size_t n = std::min(skip, sizeof(scratch));
            mic_.Take(scratch, n);
            skip -= n;
        }
    }
    if (mic_.Take(scratch, kFrameBytes) != 0u) emu_.Get<AudioActivityWidget>().MarkRx();
    left_  = static_cast<int16_t>(cerf::le::U16(scratch, 0));
    right_ = static_cast<int16_t>(cerf::le::U16(scratch, 2));
    have_  = true;
    frame_ = frame;
}

int16_t Tps65950AudioUplink::Sample(uint64_t frame, Channel ch) {
    const Path& path = path_[static_cast<uint8_t>(ch)];
    if (!path.carries) return 0;
    if (!have_ || frame != frame_) Advance(frame);
    const int32_t adc = tps65950::ScaleQ16(IsLeft(ch) ? left_ : right_, path.analog);
    return static_cast<int16_t>(tps65950::ScaleQ16(adc, path.digital));
}

REGISTER_SERVICE(Tps65950AudioUplink);

}
