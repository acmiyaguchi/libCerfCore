#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

/* Strategy for the guest CPU's cold-reset entry state. */
class BootMode : public Service {
public:
    using Service::Service;

    void OnReady() override;

    /* Initial PC at cold reset, PHYSICAL (MMU off at reset → PC consumed as PA). */
    virtual uint32_t ColdEntryPa() = 0;

    /* ARM ARM DDI 0406C.c A2.3.2 BXWritePC(), p. A2-47: "if address<0> == '1'
       then SelectInstrSet(InstrSet_Thumb)". */
    virtual bool ColdEntryThumb() { return false; }

    /* Initial SP at cold reset, PHYSICAL. */
    virtual uint32_t ColdStackPa() = 0;
};

}
