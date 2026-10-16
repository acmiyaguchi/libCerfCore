#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

namespace cerfcore {

class PcmciaSlot;

/* 16-bit PC Card static-window router. PA 0x20000000; socket = bit 28
   (256 MB each), region = bits 27:26 (64 MB: 0 I/O, 1 reserved, 2
   attribute, 3 common). Layout verified identical on SA-1110 (Dev Man
   Fig 10-21) and PXA255 (Dev Man 278693 Fig 6-26, p6-64). */
class PcmciaSpaceRouter : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x20000000u; }
    uint32_t MmioSize() const override { return 0x20000000u; }   /* 512 MB */

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Called from the board socket controller's OnReady. */
    void ProvideSockets(PcmciaSlot* socket0, PcmciaSlot* socket1);

    /* nullptr when no controller provided the socket. */
    PcmciaSlot* Socket(int n) const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    enum class Region { Io, Reserved, Attribute, Common };

    PcmciaSlot* Decode(uint32_t addr, Region* region,
                       uint32_t* card_offset) const;

    PcmciaSlot* sockets_[2] = {};
};

}
