#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

/* NEC VR41xx PIU (Touch Panel Interface Unit), VR4121 UM ch.20 / VR4102 UM ch.19.
   Control block 0x0B000120 + data buffers 0x0B0002A0 (VR4121 UM Table 20-1,
   VR4102 UM Table 19-1). */
class Vr41xxPiu : public Peripheral {
public:
    using Peripheral::Peripheral;

    virtual uint32_t Piu2Base() const = 0;
    virtual uint32_t Piu2Size() const = 0;

    /* PADDATA(9:0) is the A/D converter's 10-bit sampling data (VR4121 UM 20.3.9,
       VR4102 UM 19.3.9). */
    virtual void SetPen(bool down, uint16_t pos_x, uint16_t pos_y) = 0;

    virtual void SyntheticTap(uint16_t pos_x, uint16_t pos_y) = 0;
};
