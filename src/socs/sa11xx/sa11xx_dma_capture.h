#pragma once

#define NOMINMAX

#include <cerfcore/core/service.h>
#include "../../platform/wave_in_fifo.h"
#include "sa11xx_audio_config.h"
#include "sa11xx_dma_clients.h"

#include <cstdint>

class Sa11xxDmaCapture : public Service, public Sa11xxDmaReceiveSource {
public:
    using Service::Service;

    void OnReady() override;
    void OnShutdown() override;

    bool FillReceived(uint32_t ddar, uint32_t pa, uint32_t bytes,
                      GuestCycleClock::Rate word_rate) override;
    void OnReceiveRestored() override;

protected:
    virtual Sa11xxAudioConfig AudioConfig() const = 0;

private:
    Sa11xxAudioConfig cfg_{};
    WaveInFifo        mic_;
};
