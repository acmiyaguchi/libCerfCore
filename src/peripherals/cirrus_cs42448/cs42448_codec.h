#pragma once

#include <cstdint>

/* Cirrus Logic CS42448 audio codec I2C slave (7-bit address 0x48); registers
   0x01..0x1B, reg 0x01 (Chip I.D./Rev) and 0x19 (Status) read-only, MAP bit7 =
   address auto-increment. Values per the Cirrus CS42448 register map. */
class Cs42448Codec {
public:
    Cs42448Codec() { Reset(); }

    void Reset();
    void WriteReg(uint8_t reg, uint8_t val);
    uint8_t ReadReg(uint8_t reg) const;

private:
    static constexpr uint8_t kChipId = 0x01u;  /* read-only */
    static constexpr uint8_t kStatus = 0x19u;  /* read-only, volatile */
    static constexpr uint8_t kFirstReg = 0x01u;
    static constexpr uint8_t kLastReg = 0x1Bu;  /* MUTEC */

    uint8_t reg_[0x1Cu] = {0};
};
