#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Sed1356PowerSequence : public Service {
public:
    using Service::Service;

    virtual uint32_t LcdPowerOnLines(uint8_t power_save_reg) const = 0;
    virtual bool     LcdDisabledReadsPanelDown() const = 0;
    virtual bool     MemoryControllerPowersDown(uint8_t refresh_reg) const = 0;
};

}
