#pragma once

#include <cerfcore/core/service.h>
#include "../../platform/wave_in_fifo.h"

#include <cstdint>

namespace cerfcore {

class Sa1111SacHostInput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void SetRate(uint64_t fs_num, uint64_t fs_den);
    void Take(uint8_t* dst, uint32_t words);
    void Clear();

private:
    WaveInFifo mic_;
    uint32_t   rate_ = 0;
};

}
