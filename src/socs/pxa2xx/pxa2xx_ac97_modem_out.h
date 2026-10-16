#pragma once

#include "pxa2xx_ac97_out_fifo.h"

#include <cstdint>

class Pxa2xxAc97ModemOut : public Pxa2xxAc97OutFifo {
public:
    using Pxa2xxAc97OutFifo::Pxa2xxAc97OutFifo;

protected:
    uint32_t    FifoPa() const override;
    uint32_t    Request() const override;
    const char*    KeyPrefix() const override { return "modem"; }
    Pxa2xxSlotPace SlotPace() override;
    void           OnWord(uint32_t) override {}
    void           OnBlock(uint32_t, uint32_t) override {}
    void           OnDmaStopped() override {}
    void           OnRestored() override {}
};
