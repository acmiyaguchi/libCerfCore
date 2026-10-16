#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

namespace cerfcore {

class Pxa2xxDmaPort;

class Pxa2xxDmaRequestMap : public Peripheral {
public:
    using Peripheral::Peripheral;

    void RegisterPort(uint32_t request, Pxa2xxDmaPort* port);

protected:
    static constexpr uint32_t kMaxRequests = 75u;
    static constexpr uint8_t  kNoRequest   = 0xFFu;

    virtual uint32_t DrcmrOf(uint32_t request) const = 0;

    Pxa2xxDmaPort* PortOf(uint32_t request) const { return ports_[request]; }
    bool           MapsChannel(uint32_t request, uint32_t& ch) const;
    Pxa2xxDmaPort* MappedPortOf(uint32_t request, uint32_t& ch) const;
    bool           AnyRequestMapsLocked(uint32_t ch, uint32_t& request) const;
    uint32_t       SoleRequestOfLocked(uint32_t ch);
    [[noreturn]] void DieTwoRequests(uint32_t ch, uint32_t a, uint32_t b);

private:
    Pxa2xxDmaPort* ports_[kMaxRequests] = {};
};

}
