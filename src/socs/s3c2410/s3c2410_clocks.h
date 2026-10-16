#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>

class S3C2410Clocks : public Service {
public:
    using Service::Service;

    virtual uint64_t FclkHz()          const = 0;
    virtual uint64_t HclkHz()          const = 0;
    virtual uint64_t PclkHz()          const = 0;
    virtual uint64_t CoreClockHz()     const = 0;
    virtual bool     PwmTimerClockOn() const = 0;

    virtual void RegisterRateListener(std::function<void()> fn) = 0;

    virtual uint32_t ReadRegister (uint32_t offset)                 = 0;
    virtual void     WriteRegister(uint32_t offset, uint32_t value) = 0;
};
