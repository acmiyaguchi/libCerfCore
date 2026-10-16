#pragma once

#include <cerfcore/core/service.h>
#include "../../platform/wave_in_fifo.h"

#include <cstdint>

class Twl4030;

class Tps65950AudioUplink : public Service {
public:
    using Service::Service;

    enum class Channel : uint8_t { Left1 = 0, Left2 = 1, Right1 = 2, Right2 = 3 };

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void    Refresh();
    void    Start(uint32_t fs_hz);
    void    Stop();
    int16_t Sample(uint64_t frame, Channel ch);

private:
    struct Path {
        bool    carries = false;
        int64_t analog  = 0;
        int64_t digital = 0;
    };

    bool Carries(Channel ch, bool& from_adc) const;
    void Advance(uint64_t frame);

    Twl4030*   twl_     = nullptr;
    Path       path_[4] = {};
    WaveInFifo mic_;
    uint32_t   fs_hz_ = 0;
    bool       have_  = false;
    uint64_t   frame_ = 0;
    int16_t    left_  = 0;
    int16_t    right_ = 0;
};
