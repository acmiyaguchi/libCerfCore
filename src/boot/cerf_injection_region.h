#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class CerfInjectionRegion : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* Band VA base; reserves the PA region + installs the overlay on first
       call. Lazy so a GA board that injects no victim never reserves it, and a
       board whose OAT leaves no static-window hole only halts when a victim
       actually needs the band. Halts if no hole exists. */
    uint32_t BandVaBase();
    uint32_t BandPaBase() const;
    uint32_t BandSize() const;

private:
    uint32_t va_base_ = 0;
};
