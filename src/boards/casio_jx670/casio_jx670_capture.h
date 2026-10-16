#pragma once

#include <cerfcore/core/service.h>
#include "../../platform/wave_in_fifo.h"

#include <cstdint>

class AudioActivityWidget;
class EmulatedMemory;

class CasioJx670Capture : public Service {
public:
    using Service::Service;

    /* casio_cassiopeia_em500_ppc2000 wavedev.dll capture converter loc_F627DC @0xF62828 lh /
       @0xF62836 addiu $v1, 4 / @0xF62866 sltiu $s1, 0x1F4: one 16-bit sample per 4-byte slot. */
    static constexpr uint32_t kFrameBytes = 4u;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void Request(uint32_t rate_hz);
    void Clear();
    void WriteBlock(uint32_t va, uint32_t length);

private:
    WaveInFifo           mic_;
    EmulatedMemory*      mem_      = nullptr;
    AudioActivityWidget* activity_ = nullptr;
};
