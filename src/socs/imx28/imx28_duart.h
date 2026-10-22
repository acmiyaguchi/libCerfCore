#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <string>

namespace cerfcore {

/* MCIMX28RM Rev. 2 chapter 24 (Debug UART) at 0x8007_4000. */
class Imx28Duart : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80074000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void LoadResetValues();
    uint32_t ReadFr();
    void Transmit(uint8_t ch);
    void FlushLine();
    void WriteSub(uint32_t addr, uint32_t shift, uint32_t lane_mask, uint32_t value);
    void CheckInterruptLine(const char* cause);

    uint32_t    ilpr_ = 0;
    uint32_t    ibrd_ = 0;
    uint32_t    fbrd_ = 0;
    uint32_t    lcr_h_ = 0;
    uint32_t    div_ = 0;
    uint32_t    cr_ = 0;
    uint32_t    ifls_ = 0;
    uint32_t    imsc_ = 0;
    uint32_t    ris_ = 0;
    uint32_t    dmacr_ = 0;
    std::string tx_line_;
};

}
