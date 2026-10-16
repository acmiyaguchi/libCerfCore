#pragma once

#include "pxa2xx_ac97_in_fifo.h"
#include "pxa2xx_ac97_link.h"
#include "pxa2xx_frame_slot_law.h"
#include "pxa2xx_slot_source.h"

#include <cstdint>
#include <vector>

class Pxa2xxAc97MicIn : public Pxa2xxAc97InFifo, public Pxa2xxSlotSource {
public:
    using Pxa2xxAc97InFifo::Pxa2xxAc97InFifo;

    uint64_t Count(uint64_t frames) const override;
    bool     FrameOfSlot(uint64_t slot, uint64_t& frame) const override;
    void     Prune(uint64_t settled) override;

    void Save(StateWriter& w) override;
    void Restore(StateReader& r) override;

protected:
    uint32_t          FifoPa() const override;
    uint32_t          Request() const override;
    const char*       KeyPrefix() const override { return "mic_in"; }
    Pxa2xxSlotSource& Source() override { return *this; }
    bool              KeepsValues() const override { return true; }
    uint16_t          SlotValue(uint64_t n) override;
    void              OnRun() override;
    void              OnWrite(uint64_t next_frame) override;

private:
    enum class Owner : uint8_t { None = 0, Adc = 1, Digitiser = 2 };

    struct Span {
        uint64_t start = 0;
        Owner    owner = Owner::None;
    };

    Owner          CurrentOwner();
    Pxa2xxSlotPace AdcPace(Owner owner);
    uint64_t       DigitiserWords(uint64_t frames) const;
    size_t         SpanOf(uint64_t n) const;

    Pxa2xxFrameSlotLaw adc_{Ac97Codec::kFrameRateHz};
    std::vector<Span>  spans_;
};
