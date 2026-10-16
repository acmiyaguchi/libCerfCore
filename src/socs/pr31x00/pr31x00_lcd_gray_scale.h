#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

/* TMPR3911 §17.4.5-§17.4.14: Video Control 5-14 ($038-$05C), REDSEL / GREENSEL / BLUESEL and the
   PATn_m dither patterns. */
class Pr31x00LcdGrayScale : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x10C00038u; }
    uint32_t MmioSize() const override { return 0x28u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 LCD ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 LCD ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 LCD WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 LCD WriteHalf", addr, v); }

    uint32_t ShadeFor(uint32_t raw, uint32_t bits_per_pixel, bool inverted) const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void StorePattern(uint32_t idx, uint32_t addr, uint32_t value,
                      uint32_t recommended, const char* op);

    static constexpr uint32_t kRegs = 10;

    uint32_t reg_[kRegs] = {};
};
