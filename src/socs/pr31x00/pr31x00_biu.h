#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

namespace cerfcore {

/* Philips PR31x00 Bus Interface Unit, TMPR3911/3912 ch.4. Memory Configuration
   registers $000-$020. */
class Pr31x00Biu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x10C00000u; }
    uint32_t MmioSize() const override { return 0x24u; }   /* $000-$023 */

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 BIU ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 BIU ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 BIU WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 BIU WriteHalf", addr, v); }

    /* An access to a Card window reaches I/O space while CARDnIOEN is set and
       Attribute space while it is clear (§4.2.1). */
    bool Card1IoSpace() const;
    bool Card2IoSpace() const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void WriteConfig0(uint32_t addr, uint32_t value);
    void WriteConfig3(uint32_t addr, uint32_t value);
    void WriteConfig4(uint32_t addr, uint32_t value);

    static constexpr uint32_t kRegs = 5;   /* MEM_CONFIG0..MEM_CONFIG4 */

    /* Non-zero resets: MEM_CONFIG2 CS0ACCVAL1<7:4>/CS0ACCVAL2<3:0> = $F (§4.7.3);
       MEM_CONFIG4 MEMPOWERDOWN<16> = 1 (§4.7.5). CS0SIZE<0> follows the TESTAIU
       strap (§4.7.1), strapped high here: CS0 is a 32-bit port of two 16-bit ROMs,
       so nk.exe's flash-ID stub sees one JEDEC id replicated across both halves. */
    uint32_t reg_[kRegs] = {1u, 0u, 0xFFu, 0u, 1u << 16};
};

}
