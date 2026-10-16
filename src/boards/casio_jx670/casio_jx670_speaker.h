#pragma once

#include <cerfcore/core/service.h>
#include "../../host/paced_wave_out.h"

#include <cstdint>

namespace cerfcore {

class AudioActivityWidget;
class EmulatedMemory;

class CasioJx670Speaker : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void SetFormat(uint32_t hz, uint16_t channels, uint16_t bits);
    void Begin();
    void Finish();
    void Abort();
    void QueueBlock(uint32_t va, uint32_t length);

private:
    PacedWaveOut         paced_;
    EmulatedMemory*      mem_      = nullptr;
    AudioActivityWidget* activity_ = nullptr;
};

}
