#include "pxa2xx_ac97_bit_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/ac97_codec.h"
#include <cerfcore/state/state_stream.h>
#include "../pxa255/pxa255_id.h"
#include "../pxa27x/pxa270_id.h"

REGISTER_SERVICE(Pxa2xxAc97BitClock);

bool Pxa2xxAc97BitClock::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Pxa255 || bd->GetSocId() == SocId::Pxa270);
}

void Pxa2xxAc97BitClock::OnReady() {
    codec_ = emu_.TryGet<Ac97Codec>();
}

bool Pxa2xxAc97BitClock::Running() const {
    return unit_clock_ && (internal_ || (codec_bitclk_ && codec_->BitClockRate().num != 0u));
}

GuestCycleClock::Rate Pxa2xxAc97BitClock::Rate() const {
    return internal_ ? GuestCycleClock::Rate{kInternalHz, 1u} : PadClock();
}

GuestCycleClock::Rate Pxa2xxAc97BitClock::PadClock() const {
    return codec_bitclk_ ? codec_->BitClockRate() : GuestCycleClock::Rate{0u, 1u};
}

void Pxa2xxAc97BitClock::AddPadListener(std::function<void(uint64_t)> fn) { pad_listeners_.push_back(std::move(fn)); }

void Pxa2xxAc97BitClock::SetCodecDriving(uint64_t at, bool on) {
    if (on == codec_bitclk_) return;
    codec_bitclk_ = on;
    for (auto& fn : pad_listeners_) fn(at);
}

void Pxa2xxAc97BitClock::Start(uint64_t now) {
    SetCodecDriving(now, true);
    if (on_change_) on_change_(now);
}

void Pxa2xxAc97BitClock::ColdStart(uint64_t now) {
    if (codec_ != nullptr) Start(now);
}

void Pxa2xxAc97BitClock::Stop(uint64_t now) {
    SetCodecDriving(now, false);
}

/* Intel PXA27x Developer's Manual Table 13-8 (page 13-22) WRST: "If software attempts to perform a
   warm reset while AC97_BITCLK is running, the write is ignored". */
bool Pxa2xxAc97BitClock::WarmReset(uint64_t now) {
    if (codec_ == nullptr || internal_ || codec_bitclk_) return false;
    codec_->WarmReset();
    Start(now);
    return true;
}

void Pxa2xxAc97BitClock::CodecPoweredDown(uint64_t at) { SetCodecDriving(at, false); }

void Pxa2xxAc97BitClock::CodecPoweredUp(uint64_t at) { Start(at); }

void Pxa2xxAc97BitClock::SetUnitClock(uint64_t now, bool on) {
    if (on == unit_clock_) return;
    unit_clock_ = on;
    if (on_change_) on_change_(now);
}

void Pxa2xxAc97BitClock::SetInternal(uint64_t now, bool on) {
    if (on == internal_) return;
    internal_ = on;
    if (on_change_) on_change_(now);
}

void Pxa2xxAc97BitClock::Save(StateWriter& w) {
    w.Write<uint8_t>("link_bitclk", codec_bitclk_ ? 1u : 0u);
    w.Write<uint8_t>("link_unit_clock", unit_clock_ ? 1u : 0u);
    w.Write<uint8_t>("link_internal_bitclk", internal_ ? 1u : 0u);
}

void Pxa2xxAc97BitClock::Restore(StateReader& r) {
    uint8_t bitclk = 0, unit = 0, internal = 0;
    r.Read("link_bitclk", bitclk);
    r.Read("link_unit_clock", unit);
    r.Read("link_internal_bitclk", internal);
    codec_bitclk_ = bitclk != 0u;
    unit_clock_   = unit != 0u;
    internal_     = internal != 0u;
}
