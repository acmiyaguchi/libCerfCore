#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

namespace cerfcore {

/* NEC VR41xx ICU. Level-2 xxxINTREG & MxxxINTREG collapse into one SYSINT1/2REG bit;
   SYSINTnREG & MSYSINTnREG drives Int0..Int3 / NMI (VR4121 UM 15.1, VR4102 UM 14.1).
   Two MMIO windows, 0x0B000080 and 0x0B000200 (VR4121 UM Table 15-1, VR4102 UM
   Table 14-1). */
class Vr41xxIcu : public Peripheral {
public:
    using Peripheral::Peripheral;

    virtual uint32_t Icu2Base() const = 0;
    virtual uint32_t Icu2Size() const = 0;
    virtual uint16_t ReadHalf2(uint32_t off) = 0;
    virtual void     WriteHalf2(uint32_t off, uint16_t value) = 0;

    /* Called from peripheral threads. */
    virtual void SetSysint1Source(uint16_t bit, bool level) = 0;
    virtual void SetSysint2Source(uint16_t bit, bool level) = 0;
    virtual void SetGiuLow(uint16_t bits) = 0;
    virtual void SetGiuHigh(uint16_t bits) = 0;
    virtual void SetPiuSource(uint16_t bits) = 0;
    virtual void SetKiuSource(uint16_t bits) = 0;
    virtual void SetDsiuSource(uint16_t bits) = 0;
};

}
