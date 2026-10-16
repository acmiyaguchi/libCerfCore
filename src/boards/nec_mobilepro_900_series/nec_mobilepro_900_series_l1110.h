#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

namespace cerfcore {

/* NeoMagic NMC1110 ("L1110") PCMCIA power/reset glue: one 16-bit PRS(read)/
   PRC(write) register per socket. */
class NecMobilepro900SeriesL1110 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    virtual int Socket() const = 0;   /* 0 = CF, 1 = PC Card */

private:
    uint16_t Prs() const;
    void     WriteCommand(uint16_t value);

    uint16_t prc_ = 0;   /* last NMC1110 PRC command written */
};

}
