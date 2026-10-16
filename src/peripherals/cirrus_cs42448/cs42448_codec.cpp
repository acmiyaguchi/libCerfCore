#include "cs42448_codec.h"

void Cs42448Codec::Reset() {
    for (auto& r : reg_) r = 0u;
    /* Non-zero power-on-reset defaults (Cirrus CS42448 register map). reg 0x01
       (Chip I.D./Rev) and 0x19 (Status) stay 0: ChipID nibble 0 = the
       CS42448 family, no codec/clock errors at idle. */
    reg_[0x03u] = 0xF0u;  /* Functional Mode */
    reg_[0x04u] = 0x46u;  /* Interface Formats */
    reg_[0x06u] = 0x10u;  /* Transition Control */
}

void Cs42448Codec::WriteReg(uint8_t reg, uint8_t val) {
    if (reg < kFirstReg || reg > kLastReg) return;
    if (reg == kChipId || reg == kStatus) return;
    reg_[reg] = val;
}

uint8_t Cs42448Codec::ReadReg(uint8_t reg) const {
    if (reg < kFirstReg || reg > kLastReg) return 0u;
    return reg_[reg];
}
