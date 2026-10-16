#pragma once

#include <cerfcore/core/service.h>
#include "../../platform/wave_in_fifo.h"

#include <cstdint>
#include <deque>

class Pr31x00SibCodec;

class Pr31x00SibCapture : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void     Request(uint32_t hz, uint16_t channels);
    void     Clear();
    uint32_t Received(uint64_t rx_tick, bool sixteen_bit);
    uint32_t Ahead(uint64_t tick, uint64_t rx_tick, bool sixteen_bit);
    uint32_t TelecomReceived();

private:
    uint32_t NextHostWord(bool sixteen_bit);

    Pr31x00SibCodec*     codec_ = nullptr;
    WaveInFifo           mic_;
    std::deque<uint32_t> ahead_;
    uint64_t             ahead_tick_ = 0;
};
