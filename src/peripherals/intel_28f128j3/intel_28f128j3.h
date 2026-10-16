#pragma once

#include "../intel_command_set_flash.h"

#include <cstdint>

/* Intel 28F128J3 StrataFlash (Intel order 290667): mfr 0x89, device 0x18,
   16 MB, 128 KB uniform erase blocks. */
class Intel28F128J3 : public IntelCommandSetFlash {
public:
    using IntelCommandSetFlash::IntelCommandSetFlash;

protected:
    uint16_t Manufacturer() const override { return 0x0089u; }
    uint16_t Device()       const override { return 0x0018u; }
    uint32_t ChipEraseBlockBytes() const override { return 0x20000u; }
    const uint8_t* CfiTable() const override;
    uint32_t       CfiCount() const override;
};
