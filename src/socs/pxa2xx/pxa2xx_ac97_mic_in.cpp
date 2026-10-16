#include "pxa2xx_ac97_mic_in.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/ac97_codec.h"
#include <cerfcore/state/state_stream.h>

namespace {

/* Intel PXA255 Developer's Manual Table 5-5 (page 5-13) and Intel PXA27x Developer's Manual Table 5-10
   (page 5-24): AC97 microphone FIFO 0x4050_0060 on DRCMR 0x4000_0120. */
constexpr uint32_t kMcdr      = 0x40500060u;
constexpr uint32_t kRequestRx = 8u;
/* Intel PXA255 Developer's Manual section 13.4.2.7 (page 13-11): slot 6 "Optional Dedicated Microphone
   Record Data". */
constexpr uint32_t kMicSlot = 6u;

}  // namespace

uint32_t Pxa2xxAc97MicIn::FifoPa() const { return kMcdr; }
uint32_t Pxa2xxAc97MicIn::Request() const { return kRequestRx; }

/* Cirrus Logic WM9713L Rev 4.0 Table 10 (page 32) and Wolfson WM9705 Table 22 (page 38): ASS 10 puts the left
   ADC data on slot 6. */
Pxa2xxAc97MicIn::Owner Pxa2xxAc97MicIn::CurrentOwner() {
    if (codec_->DigitiserSlot() == kMicSlot) return Owner::Digitiser;
    if (codec_->AdcPowered() && codec_->AdcLeftSlot() == kMicSlot) return Owner::Adc;
    return Owner::None;
}

Pxa2xxSlotPace Pxa2xxAc97MicIn::AdcPace(Owner owner) {
    if (owner != Owner::Adc) return Pxa2xxSlotPace{0u, 1u};
    return Pxa2xxSlotPace{codec_->AdcRateHz(), codec_->ConverterDivider()};
}

uint64_t Pxa2xxAc97MicIn::DigitiserWords(uint64_t frames) const {
    return codec_->SlotWordsBefore(kMicSlot, frames);
}

uint64_t Pxa2xxAc97MicIn::Count(uint64_t frames) const { return adc_.Count(frames) + DigitiserWords(frames); }

size_t Pxa2xxAc97MicIn::SpanOf(uint64_t n) const {
    for (size_t i = 0; i + 1u < spans_.size(); ++i) {
        if (n <= Count(spans_[i + 1u].start)) return i;
    }
    return spans_.size() - 1u;
}

bool Pxa2xxAc97MicIn::FrameOfSlot(uint64_t slot, uint64_t& frame) const {
    if (spans_.empty()) return false;
    const Span&    s = spans_[SpanOf(slot)];
    const uint64_t k = slot - Count(s.start);
    switch (s.owner) {
    case Owner::Adc:       return adc_.FrameOfSlot(adc_.Count(s.start) + k, frame);
    case Owner::Digitiser: return codec_->FrameOfSlotWord(kMicSlot, DigitiserWords(s.start) + k, frame);
    default:               return false;
    }
}

uint16_t Pxa2xxAc97MicIn::SlotValue(uint64_t n) {
    const Span& s = spans_[SpanOf(n)];
    if (s.owner != Owner::Digitiser) return 0u;
    return codec_->SlotWord(kMicSlot, DigitiserWords(s.start) + (n - Count(s.start)));
}

void Pxa2xxAc97MicIn::Prune(uint64_t settled) {
    adc_.Prune(settled);
    codec_->PruneSlotWords(kMicSlot, settled);
    while (spans_.size() > 1u && spans_[1].start <= settled) spans_.erase(spans_.begin());
    if (!spans_.empty() && spans_[0].start < settled) spans_[0].start = settled;
}

void Pxa2xxAc97MicIn::OnRun() {
    const Owner owner = CurrentOwner();
    adc_.Reset(0u, AdcPace(owner));
    spans_.assign(1u, Span{0u, owner});
}

void Pxa2xxAc97MicIn::OnWrite(uint64_t next_frame) {
    const Owner owner = CurrentOwner();
    adc_.Change(next_frame, AdcPace(owner));
    if (spans_.empty() || spans_.back().owner == owner) return;
    LOG(SocIis, "AC'97 mic in slot owner %u from frame %llu\n", static_cast<unsigned>(owner),
        static_cast<unsigned long long>(next_frame));
    if (spans_.back().start >= next_frame) {
        spans_.back().owner = owner;
        return;
    }
    spans_.push_back(Span{next_frame, owner});
}

void Pxa2xxAc97MicIn::Save(StateWriter& w) {
    Pxa2xxAc97InFifo::Save(w);
    adc_.Save(w, "mic_in_adc");
    w.Write<uint32_t>("mic_in_spans", static_cast<uint32_t>(spans_.size()));
    for (const Span& s : spans_) {
        w.Write<uint64_t>("mic_in_span_start", s.start);
        w.Write<uint8_t>("mic_in_span_owner", static_cast<uint8_t>(s.owner));
    }
}

void Pxa2xxAc97MicIn::Restore(StateReader& r) {
    Pxa2xxAc97InFifo::Restore(r);
    adc_.Restore(r, "mic_in_adc");
    uint32_t n = 0;
    r.Read("mic_in_spans", n);
    spans_.assign(n, Span{});
    for (Span& s : spans_) {
        uint8_t owner = 0;
        r.Read("mic_in_span_start", s.start);
        r.Read("mic_in_span_owner", owner);
        s.owner = static_cast<Owner>(owner);
    }
}

REGISTER_SERVICE(Pxa2xxAc97MicIn);
