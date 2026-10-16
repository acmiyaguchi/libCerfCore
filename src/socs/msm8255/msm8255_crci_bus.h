#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Msm8255CrciClient {
public:
    virtual ~Msm8255CrciClient() = default;

    virtual void AssertCrci(uint32_t crci) = 0;
};

class Msm8255CrciBus : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Register(Msm8255CrciClient* client);
    void Assert(uint32_t crci);
    void Deassert(uint32_t crci);
    bool LevelHigh(uint32_t crci) const;

    void DeclareFifo(uint32_t crci, uint32_t pa, uint32_t bytes);
    bool Declared(uint32_t crci) const;
    bool FifoCovers(uint32_t crci, uint32_t pa) const;

    static constexpr uint32_t kCrciCount = 16u;

private:
    struct Fifo {
        uint32_t pa    = 0u;
        uint32_t bytes = 0u;
    };

    Msm8255CrciClient* client_ = nullptr;
    Fifo               fifos_[kCrciCount];
    bool               level_[kCrciCount] = {};
};

}
