#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace cerfcore {

/* keybddr.dll matrix scanner at PA 0x6C000000 (sub_14B12E4 maps phys
   0x6C000000; sub_14B166C reads column c at offset (2<<c); sub_14B17D0
   rawcode = col*16+row): read16(off) = OR of columns c where off bit
   (c+1) is set, over their pressed-row masks. */
class SharpMobilonHcKeyMatrix : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override;

    void SetKey(uint8_t col, uint8_t row, bool down);

    void RestoreState(StateReader& r) override;

private:
    static constexpr uint32_t kBase = 0x6C000000u;
    static constexpr uint32_t kSize = 0x1000u;

    std::array<std::atomic<uint16_t>, 8> rows_{};
};

}
