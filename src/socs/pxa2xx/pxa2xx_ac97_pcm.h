#pragma once

#include "../../host/paced_wave_out.h"
#include "pxa2xx_ac97_out_fifo.h"

#include <cstdint>
#include <vector>

class Pxa2xxAc97BitClock;

class Pxa2xxAc97Pcm : public Pxa2xxAc97OutFifo {
public:
    using Pxa2xxAc97OutFifo::Pxa2xxAc97OutFifo;

    void OnReady() override;
    void OnShutdown() override;

protected:
    uint32_t    FifoPa() const override;
    uint32_t    Request() const override;
    const char*    KeyPrefix() const override { return "pcm"; }
    Pxa2xxSlotPace SlotPace() override;
    void           OnWord(uint32_t value) override;
    void           OnBlock(uint32_t pa, uint32_t bytes) override;
    void           OnDmaStopped() override;
    void           OnRestored() override;

private:
    void     QueueHost(const void* bytes, uint32_t length);
    uint32_t HostRate();

    PacedWaveOut         host_;
    Pxa2xxAc97BitClock*  bit_clock_   = nullptr;
    bool                 host_active_ = false;
    uint32_t             host_rate_   = 0;
    std::vector<uint8_t> block_;
    std::vector<uint8_t> masked_;
};
