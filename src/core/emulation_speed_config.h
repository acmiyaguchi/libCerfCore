#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <string>

namespace cerfcore {

class EmulationSpeedConfig : public Service {
public:
    struct Multiplier {
        uint64_t num = 1u;
        uint64_t den = 1u;

        bool IsOne() const { return num == den; }
    };

    using Service::Service;

    void OnReady() override;

    Multiplier Throttle() const { return throttle_; }
    Multiplier CpuRate() const { return cpu_rate_; }

private:
    void Load(const std::string& path);

    Multiplier throttle_;
    Multiplier cpu_rate_;
};

}
