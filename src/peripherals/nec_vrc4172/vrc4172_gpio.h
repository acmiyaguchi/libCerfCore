#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <mutex>

/* NEC VRC4172 companion GPIO + Level-2 GPIO interrupt controller @ PA
   0x15001080. Register map + trigger semantics: NetBSD hpcmips vrc4172gpioreg.h
   + vrc4172gpio.c. */
class Vrc4172Gpio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x15001080u; }
    uint32_t MmioSize() const override { return 0x4Cu; }   /* 0x00-0x4A register block */

    uint8_t  ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint32_t ReadWord(uint32_t addr) override { HaltUnsupportedAccess("VRC4172 GPIO ReadWord", addr, 0); }
    void     WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("VRC4172 GPIO WriteWord", addr, v); }

    /* A board companion-chip input source drives VRC4172 GPIO pin `pin` (0..15)
       to `level` here; the chip evaluates the configured trigger and drives its
       aggregate interrupt to VR4102 GIU pin 1. */
    void SetPinLevel(int pin, bool level);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    uint16_t ReadReg16Locked(uint32_t off);
    void     DriveGiuLocked();

    mutable std::mutex mtx_;

    /* Bank-0 registers (pins 0-15). INTLV0L/H are needed by the trigger eval but
       are never accessed via MMIO by the guest (reset value 0 -> negative-edge
       for the guest's edge-armed pins), so their offsets stay FATAL-first. */
    uint16_t dir_      = 0;            /* EXGPDIR0   (1=out, 0=in) */
    uint16_t inten_    = 0;            /* EXGPINTEN0 per-pin enable */
    uint16_t intst_    = 0;            /* EXGPINTST0 latched status (W1C) */
    uint16_t inttyp_   = 0;            /* EXGPINTTYP0 (1=edge, 0=level) */
    uint16_t intlv0l_  = 0;            /* EXGPINTLV0L polarity, pins 0-7 */
    uint16_t intlv0h_  = 0;            /* EXGPINTLV0H polarity, pins 8-15 */

    /* GPIO3 = the MobilePro 700 cold-boot/wake status line, high on cold boot
       (OAL start() 0x9F001C38 reads DATA0 & 8; bit3 CLEAR -> cop0 0x23 HIBERNATE
       which CERF FATALs). Current input pin levels, bit set = high. */
    static constexpr uint16_t kColdBootPin = 0x0008u;   /* GPIO3 input */
    uint16_t level_ = kColdBootPin;

    /* Last aggregate INT level driven onto GIU pin 1 (derived; not serialized). */
    bool giu_asserted_ = false;
};
