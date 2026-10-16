#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Twl4030;

class Tps65950AudioDownlink : public Service {
public:
    using Service::Service;

    enum Sdr : uint8_t { kL1 = 0, kR1 = 1, kL2 = 2, kR2 = 3 };

    bool ShouldRegister() override;
    void OnReady() override;

    void Refresh();
    void Mix(const int32_t (&sdr)[4], int16_t& left, int16_t& right) const;

private:
    struct Signal {
        int32_t half[4] = {};
    };

    struct Channel {
        Signal  input;
        int64_t digital = 0;
        double  analog  = 0.0;
    };

    using Sides = double[2][4];

    Channel AnalogChannel(Sdr ch) const;
    Signal  SdrSelection(Sdr ch) const;
    bool    SdrOn(Sdr ch) const;
    bool    VoiceActive() const;
    void    RequireModelled() const;
    void    AddOutput(Sides& out, int side, uint8_t sources, const Channel (&channel)[4], const uint8_t (&bit)[4],
                      bool voice, double gain) const;

    Twl4030* twl_ = nullptr;
    int32_t  input_[4][4]  = {};
    int64_t  digital_[4]   = {};
    int64_t  weight_[2][4] = {};
};

}
