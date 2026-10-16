#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class CasioJx670RateLadder : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    uint32_t RequireRate(uint32_t select, bool doubler) const;

private:
    bool Lookup(uint32_t select, bool doubler, uint32_t& hz) const;
};
