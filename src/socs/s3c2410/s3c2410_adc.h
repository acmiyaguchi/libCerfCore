#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "s3c2410_sub_source_levels.h"

#include <cstdint>
#include <mutex>

namespace cerfcore {

/* S3C2410A UM Table 1-4 p. 1-35 "A/D converter": ADCCON 0x58000000,
   ADCTSC 0x58000004, ADCDLY 0x58000008, ADCDAT0 0x5800000C, ADCDAT1
   0x58000010; Acc. Unit W for all five, R/W for the first three and R for
   the two data registers. */
class S3C2410Adc : public Peripheral, public S3C2410LevelSubSource {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    /* UM p. 16-10 ADCDAT0 UPDOWN: 0 = Stylus down, 1 = Stylus up. */
    void SetPen(bool down, uint16_t sample_x, uint16_t sample_y);

    bool SubSourceAsserted(int sub_source_bit) override;

private:
    void     ConvertLocked();
    uint32_t ComposeDataLocked(uint32_t data) const;
    bool     PenInterruptAssertedLocked() const;

    std::mutex state_mutex_;

    /* UM p. 16-7 / 16-8 / 16-9 Reset Value columns. */
    uint32_t con_ = 0x3FC4u;
    uint32_t tsc_ = 0x0058u;
    uint32_t dly_ = 0x00FFu;

    bool     ecflg_    = false;
    bool     pen_down_ = false;
    uint16_t pen_x_    = 0;
    uint16_t pen_y_    = 0;
    uint32_t x_data_   = 0;
    uint32_t y_data_   = 0;
};

}
