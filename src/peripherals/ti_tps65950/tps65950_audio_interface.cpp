#include "tps65950_audio_interface.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ti_omap35x_evm/ti_omap35x_evm_family_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../host/audio_activity_widget.h"
#include <cerfcore/state/state_stream.h>
#include "tps65950_audio_downlink.h"
#include "tps65950_audio_uplink.h"
#include "tps65950_board.h"
#include "twl4030.h"

#include <algorithm>
#include <cstddef>

namespace cerfcore {

namespace {

/* TPS65950 TRM SWCU050E Table 14-15 CODEC_MODE: APLL_RATE [7:4], CODECPDZ [1], OPT_MODE [0]; Table 14-27
   AUDIO_IF: AIF_SLAVE_EN [7], DATA_WIDTH [6:5], AIF_FORMAT [4:3], AIF_TRI_EN [2], CLK256FS_EN [1], AIF_EN
   [0]; Table 14-71 APLL_CTL: APLL_EN [4], APLL_INFREQ [3:0] (5, 6 and 0xF defined). */
constexpr uint8_t kCodecMode = 0x01u, kAudioIf = 0x0Eu, kApllCtl = 0x3Au;
constexpr uint8_t kCodecPdz  = 1u << 1;
constexpr uint8_t kOptMode   = 1u << 0;
constexpr uint8_t kAifSlave  = 1u << 7;
constexpr uint8_t kAifTriEn  = 1u << 2;
constexpr uint8_t kClk256Fs  = 1u << 1;
constexpr uint8_t kAifEn     = 1u << 0;
constexpr uint8_t kApllEn    = 1u << 4;
constexpr uint8_t kFormatLj  = 1u, kFormatRj = 2u, kFormatTdm = 3u;
constexpr uint8_t kWidth16   = 0u, kWidthReserved = 1u, kWidth32Of24 = 3u;

constexpr uint16_t kChannels = 2u, kBits = 16u;

/* TPS65950 TRM SWCU050E Table 14-71 APLL_CTL (page 774): APLL_INFREQ 0x5 19.2 MHz, 0x6 26 MHz, 0xF 38.4 MHz. */
uint64_t ApllInfreqHz(uint8_t code) {
    return code == 0x5u ? 19200000u : code == 0x6u ? 26000000u : code == 0xFu ? 38400000u : 0u;
}

uint32_t ApllRateHz(uint8_t code) {
    switch (code) {
    case 0x0: return 8000u;
    case 0x1: return 11025u;
    case 0x2: return 12000u;
    case 0x4: return 16000u;
    case 0x5: return 22050u;
    case 0x6: return 24000u;
    case 0x8: return 32000u;
    case 0x9: return 44100u;
    case 0xA: return 48000u;
    case 0xE: return 96000u;
    default:  return 0u;
    }
}

}

bool Tps65950AudioInterface::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
}

void Tps65950AudioInterface::OnReady() {
    clock_      = &emu_.Get<GuestCycleClock>();
    twl_        = &emu_.Get<Twl4030>();
    uplink_     = &emu_.Get<Tps65950AudioUplink>();
    downlink_   = &emu_.Get<Tps65950AudioDownlink>();
    hfclkin_hz_ = emu_.Get<Tps65950Board>().HfclkinHz();
    clock_->RegisterRateListener([this] { OnCpuRate(); });
    host_.Start("Tps65950Audio", 0u, kChannels, kBits, true);
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void Tps65950AudioInterface::OnShutdown() { host_.Stop(); }

/* SWCU050E Table 14-27: CODECPDZ off switches the audio interfaces off; section 14.2.1.1 (page 684): TDM is
   master only, a slave provides 256*Fs (128*Fs at 96 kHz); page 724 step 17: CLK256FS only in slave mode. */
Tps65950AudioInterface::Config Tps65950AudioInterface::ReadConfig() const {
    const uint8_t mode = twl_->AudioReg(kCodecMode);
    const uint8_t aif  = twl_->AudioReg(kAudioIf);
    const uint8_t apll = twl_->AudioReg(kApllCtl);
    Config c;
    if ((mode & kCodecPdz) == 0u || (apll & kApllEn) == 0u) return c;
    auto& fatal = emu_.Get<Fatal>();
    c.infreq_hz = ApllInfreqHz(static_cast<uint8_t>(apll & 0xFu));
    if (c.infreq_hz == 0u) {
        fatal.Die("Tps65950Audio: APLL_CTL 0x%02X selects reserved APLL_INFREQ 0x%X", apll, apll & 0xFu);
    }
    c.fs_hz = ApllRateHz(static_cast<uint8_t>(mode >> 4));
    if (c.fs_hz == 0u) {
        fatal.Die("Tps65950Audio: CODEC_MODE 0x%02X selects reserved APLL_RATE 0x%X", mode, mode >> 4);
    }
    const bool    slave  = (aif & kAifSlave) != 0u;
    const uint8_t width  = static_cast<uint8_t>((aif >> 5) & 3u);
    c.format             = static_cast<uint8_t>((aif >> 3) & 3u);
    if ((aif & kClk256Fs) != 0u && !slave) {
        fatal.Die("Tps65950Audio: AUDIO_IF 0x%02X enables CLK256FS in master mode", aif);
    }
    if ((aif & kAifEn) != 0u) {
        if (width == kWidthReserved) fatal.Die("Tps65950Audio: AUDIO_IF 0x%02X selects reserved DATA_WIDTH 1", aif);
        if (c.format == kFormatTdm && (mode & kOptMode) == 0u) {
            fatal.Die("Tps65950Audio: AUDIO_IF 0x%02X selects TDM with CODEC_MODE 0x%02X option 2", aif, mode);
        }
        if (c.format == kFormatTdm && slave) {
            fatal.Die("Tps65950Audio: AUDIO_IF 0x%02X selects TDM in slave mode", aif);
        }
    }
    c.slot_bits  = width == kWidth16 ? 16u : 32u;
    c.word_bits  = width == kWidth32Of24 ? 24u : 16u;
    c.frame_bits = (c.format == kFormatTdm ? 4u : 2u) * c.slot_bits;
    c.sink_on    = (aif & kAifEn) != 0u;
    c.drives_bus = c.sink_on && !slave && (aif & kAifTriEn) == 0u;
    c.pad_on     = (aif & kClk256Fs) != 0u && slave;
    c.dout_on    = c.sink_on && (aif & kAifTriEn) == 0u;
    return c;
}

void Tps65950AudioInterface::OnAudioRegisterWillChange() {
    listener_->OnPeerTimingWillChange(clock_->Cycles());
}

void Tps65950AudioInterface::OnAudioRegisterWrite() { Apply(ReadConfig()); }

/* SWCU050E section 14.2.1.1 (page 684): "TDM/I2S timing signals are generated from the external HFCLKIN
   clock"; "A PLL in the device audio block generates the appropriate frequencies from HFCLKIN". */
GuestCycleClock::Rate Tps65950AudioInterface::ApllScaled(uint64_t nominal_hz) const {
    return GuestCycleClock::Rate{nominal_hz * hfclkin_hz_, cfg_.infreq_hz}.Reduced();
}

GuestCycleClock::Rate Tps65950AudioInterface::BitRate() const {
    return ApllScaled(static_cast<uint64_t>(cfg_.fs_hz) * cfg_.frame_bits);
}

GuestCycleClock::Rate Tps65950AudioInterface::PadRate() const {
    if (!cfg_.pad_on) return GuestCycleClock::Rate{0u, 1u};
    return ApllScaled(static_cast<uint64_t>(cfg_.fs_hz) * (cfg_.fs_hz == 96000u ? 128u : 256u));
}

void Tps65950AudioInterface::Apply(const Config& next) {
    if (next == cfg_) return;
    const uint64_t now        = clock_->Cycles();
    const bool     bus_change = next.drives_bus != cfg_.drives_bus ||
                                (next.drives_bus && (next.fs_hz != cfg_.fs_hz ||
                                                     next.infreq_hz != cfg_.infreq_hz ||
                                                     next.frame_bits != cfg_.frame_bits ||
                                                     next.format != cfg_.format));
    const GuestCycleClock::Rate pad_before = PadRate();
    if (bus_change) listener_->OnPeerTimingWillChange(now);
    const bool sink_change = next.sink_on != cfg_.sink_on || next.fs_hz != cfg_.fs_hz ||
                             next.format != cfg_.format || next.frame_bits != cfg_.frame_bits;
    if (sink_change) FlushFrames();
    cfg_ = next;
    if (bus_change && cfg_.drives_bus) {
        bits_.SetRate(clock_->ClockRate(), BitRate());
        bits_.PlaceAt(now, RatedTickCount::Position{});
    }
    if (sink_change) StartHost();
    LOG(Periph, "[TPS65950] audio interface %u Hz format %u slot %u word %u sink %d master %d pad %d\n",
        cfg_.fs_hz, cfg_.format, cfg_.slot_bits, cfg_.word_bits, cfg_.sink_on ? 1 : 0,
        cfg_.drives_bus ? 1 : 0, cfg_.pad_on ? 1 : 0);
    if (bus_change) listener_->OnPeerRestarted(now);
    if (!(PadRate() == pad_before)) {
        for (auto& fn : pad_listeners_) fn();
    }
}

void Tps65950AudioInterface::OnCpuRate() {
    if (!cfg_.drives_bus) return;
    const uint64_t now = clock_->Cycles();
    listener_->OnPeerTimingWillChange(now);
    bits_.Rescale(now, clock_->ClockRate(), BitRate());
    listener_->OnPeerRescaled(now);
}

void Tps65950AudioInterface::StartHost() {
    if (!cfg_.sink_on) {
        uplink_->Stop();
        if (host_active_) {
            host_.FinishAudioOut();
            host_active_ = false;
        }
        return;
    }
    uplink_->Start(cfg_.fs_hz);
    host_.SetFormat(cfg_.fs_hz, kChannels, kBits);
    if (!host_active_) {
        host_.BeginAudioOut();
        host_active_ = true;
    }
}

/* SWCU050E Figures 14-23, 14-24, 14-31: codec mode, SYNC low = left; Figure 14-32: left- / right-justified
   16-bit slots, SYNC low = left; Figures 14-25 to 14-28: 32-bit slots, SYNC high = left; Figures 14-29,
   14-30, 14-33: TDM, a one-clock SYNC pulse that falls at the frame start. */
bool Tps65950AudioInterface::HighLeft() const {
    return (cfg_.format == kFormatLj || cfg_.format == kFormatRj) && cfg_.slot_bits == 32u;
}

uint32_t Tps65950AudioInterface::SyncRise() const {
    if (cfg_.format == kFormatTdm) return cfg_.frame_bits - 1u;
    return HighLeft() ? 0u : cfg_.frame_bits / 2u;
}

uint32_t Tps65950AudioInterface::SyncFall() const {
    if (cfg_.format == kFormatTdm) return 0u;
    return HighLeft() ? cfg_.frame_bits / 2u : 0u;
}

void Tps65950AudioInterface::SetPeerListener(Omap3530McBspPeerListener* listener) {
    if (listener_ != nullptr) emu_.Get<Fatal>().Die("Tps65950Audio: a second McBSP port listener registered");
    listener_ = listener;
}

void Tps65950AudioInterface::RegisterPadListener(std::function<void()> fn) {
    pad_listeners_.push_back(std::move(fn));
}

void Tps65950AudioInterface::ReceiveFrame(const Omap3530McBspDxFrame& frame) {
    if (!cfg_.sink_on || !frame.has_sync) {
        history_.clear();
        return;
    }
    if (!history_.empty() && frame.index != history_.back().index + 1u) FinishHistory();
    if (!history_.empty()) {
        Decode(history_.back(), history_.size() >= 2u ? &history_[history_.size() - 2u] : nullptr, &frame);
    }
    history_.push_back(frame);
    if (history_.size() > 2u) history_.pop_front();
}

void Tps65950AudioInterface::FinishHistory() {
    if (!history_.empty()) {
        Decode(history_.back(), history_.size() >= 2u ? &history_[history_.size() - 2u] : nullptr, nullptr);
    }
    history_.clear();
}

/* SWCU050E Table 14-27 AIF_EN 0: "Off mode DIN/DOUT/SYN/CLK = L". */
void Tps65950AudioInterface::FlushFrames() {
    FinishHistory();
    const size_t whole = HostBlockBytes();
    if (block_.size() % whole != 0u) block_.resize((block_.size() / whole + 1u) * whole, 0u);
    PushWholeBlocks();
}

size_t Tps65950AudioInterface::HostBlockBytes() const {
    return static_cast<size_t>(host_.HostBlockFrames()) * kChannels * (kBits / 8u);
}

void Tps65950AudioInterface::PushWholeBlocks() {
    const size_t push = block_.size() / HostBlockBytes() * HostBlockBytes();
    if (push == 0u) return;
    if (host_active_) host_.QueueOutputInHostBlocks(block_.data(), static_cast<uint32_t>(push));
    block_.erase(block_.begin(), block_.begin() + static_cast<std::ptrdiff_t>(push));
    emu_.Get<AudioActivityWidget>().MarkTx();
}

int Tps65950AudioInterface::BitAt(const Omap3530McBspDxFrame* frame, int64_t offset) {
    if (frame == nullptr || offset < 0) return -1;
    for (const auto& w : frame->words) {
        if (offset >= w.start && offset < static_cast<int64_t>(w.start) + w.bits) {
            return static_cast<int>((w.value >> (w.bits - 1u - static_cast<uint32_t>(offset - w.start))) & 1u);
        }
    }
    return -1;
}

int16_t Tps65950AudioInterface::WordAt(int64_t start, const Omap3530McBspDxFrame& cur,
                                       const Omap3530McBspDxFrame* prev,
                                       const Omap3530McBspDxFrame* next) const {
    uint32_t v = 0u;
    for (uint32_t i = 0; i < cfg_.word_bits; ++i) {
        const int64_t p = start + i;
        int bit = BitAt(&cur, p);
        if (bit < 0 && next != nullptr) bit = BitAt(next, p - cur.period);
        if (bit < 0 && prev != nullptr) bit = BitAt(prev, p + prev->period);
        v = (v << 1) | (bit > 0 ? 1u : 0u);
    }
    return static_cast<int16_t>(cfg_.word_bits == 24u ? v >> 8 : v);
}

/* SWCU050E Figures 14-31 / 14-32 (page 720): codec words one clock after their SYNC edge, 16-bit justified words at it;
   Figures 14-25 / 14-27 (page 717): 32-bit slots SYNC high = left, right-justified words end at the next edge;
   Figure 14-33 (page 720): the four TDM slots from the SYNC fall. */
int64_t Tps65950AudioInterface::WordStart(const Omap3530McBspDxFrame& frame, uint32_t k) const {
    const int64_t p = frame.period, r = frame.sync_rise, f = frame.sync_fall, w = cfg_.word_bits;
    const bool    left = k == 0u;
    if (cfg_.format == kFormatTdm) return f + static_cast<int64_t>(k) * cfg_.slot_bits;
    if (cfg_.format == 0u) return (left ? f : r) + 1;
    if (cfg_.slot_bits == 16u) return left ? f : r;
    if (cfg_.format == kFormatLj) return left ? r : f;
    return left ? f + (f > r ? 0 : p) - w : r + (r > f ? 0 : p) - w;
}

/* SWCU050E Figure 14-33 (page 720): TDM RX channels 1-4 are Left 2, Left 1, Right 2, Right 1. */
void Tps65950AudioInterface::Decode(const Omap3530McBspDxFrame& cur, const Omap3530McBspDxFrame* prev,
                                    const Omap3530McBspDxFrame* next) {
    using Sdr = Tps65950AudioDownlink::Sdr;
    int32_t sdr[4] = {};
    if (cfg_.format == kFormatTdm) {
        static constexpr Sdr kSlots[4] = {Sdr::kL2, Sdr::kL1, Sdr::kR2, Sdr::kR1};
        for (uint32_t k = 0; k < 4u; ++k) sdr[kSlots[k]] = WordAt(WordStart(cur, k), cur, prev, next);
    } else {
        sdr[Sdr::kL1] = sdr[Sdr::kL2] = WordAt(WordStart(cur, 0u), cur, prev, next);
        sdr[Sdr::kR1] = sdr[Sdr::kR2] = WordAt(WordStart(cur, 1u), cur, prev, next);
    }
    int16_t out_left = 0, out_right = 0;
    downlink_->Mix(sdr, out_left, out_right);
    cerf::le::Append16(block_, static_cast<uint16_t>(out_left));
    cerf::le::Append16(block_, static_cast<uint16_t>(out_right));
    PushWholeBlocks();
}

/* TPS65950 TRM SWCU050E Figure 14-27 (page 717): right-justified TX words after the padding zeros; Figure 14-33
   (page 720): TDM TX channels 1-4 are Left 1, Left 2, Right 1, Right 2. */
uint32_t Tps65950AudioInterface::DoutWord(const Omap3530McBspDxFrame& shape, uint32_t start, uint32_t bits) {
    if (!cfg_.dout_on || !shape.has_sync || shape.period == 0u) return 0u;
    using Ch = Tps65950AudioUplink::Channel;
    static constexpr Ch kTdm[4]  = {Ch::Left1, Ch::Left2, Ch::Right1, Ch::Right2};
    static constexpr Ch kPair[2] = {Ch::Left1, Ch::Right1};
    const bool     tdm    = cfg_.format == kFormatTdm;
    const uint32_t count  = tdm ? 4u : 2u;
    const int64_t  p      = shape.period;
    const int64_t  w      = cfg_.word_bits;
    const int64_t  first  = start;
    const int64_t  last   = first + bits;
    uint32_t       result = 0u;
    for (int64_t q = -1; q <= 1; ++q) {
        if (q < 0 && shape.index == 0u) continue;
        const uint64_t frame = static_cast<uint64_t>(static_cast<int64_t>(shape.index) + q);
        for (uint32_t k = 0; k < count; ++k) {
            const int64_t ws = WordStart(shape, k) + q * p;
            const int64_t lo = std::max(ws, first);
            const int64_t hi = std::min(ws + w, last);
            if (lo >= hi) continue;
            const int16_t  sample = uplink_->Sample(frame, tdm ? kTdm[k] : kPair[k]);
            const uint32_t value  = w == 24 ? (static_cast<uint32_t>(static_cast<int32_t>(sample)) << 8) & 0xFFFFFFu
                                            : static_cast<uint16_t>(sample);
            for (int64_t pos = lo; pos < hi; ++pos) {
                result |= ((value >> (w - 1 - (pos - ws))) & 1u) << (last - 1 - pos);
            }
        }
    }
    return result;
}

void Tps65950AudioInterface::Save(StateWriter& w) {
    const RatedTickCount::Position pos = cfg_.drives_bus ? bits_.PositionAt(clock_->Cycles())
                                                         : RatedTickCount::Position{};
    w.Write<uint64_t>("tps_audio_bits", pos.ticks);
    w.Write<uint64_t>("tps_audio_phase", pos.phase);
    w.Write<uint64_t>("tps_audio_phase_den", pos.phase_den);
}

void Tps65950AudioInterface::Restore(StateReader& r) {
    RatedTickCount::Position pos;
    r.Read("tps_audio_bits", pos.ticks);
    r.Read("tps_audio_phase", pos.phase);
    r.Read("tps_audio_phase_den", pos.phase_den);
    history_.clear();
    block_.clear();
    host_.StopAudioOut();
    host_active_ = false;
    uplink_->Stop();
    cfg_         = ReadConfig();
    if (cfg_.drives_bus) {
        bits_.SetRate(clock_->ClockRate(), BitRate());
        bits_.PlaceAt(clock_->Cycles(), pos);
    }
    StartHost();
}

REGISTER_SERVICE(Tps65950AudioInterface);

}
