#pragma once

#include "../../peripherals/uart16550/uart16550.h"

#include <cstdint>

/* VR4131 UM U15350EJ2V0UM Table 18-1 p343: DSIU = 16550 registers DSIURB..DSIUSC at
   0x0F000820-0x0F000827 (NetBSD vripreg.h VR4122_DSIU_ADDR); its reset bit lives in the
   SIU's SIURESET 0x0F000809 D1 (18.2.13 p357). */
class Vr4122Dsiu : public Uart16550 {
public:
    explicit Vr4122Dsiu(CerfEmulator& emu)
        : Uart16550(emu, Config{/*ier_mask=*/0x0Fu,
                                /*irq_gate_mcr=*/0u,
                                /*irq_gate_ier=*/0u}) {}

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x0F000820u; }
    uint32_t MmioSize() const override { return 0x08u; }

    /* nk_main_kernel.exe @0x9F033C28: `sh 0x1E -> 0xAF000820` = DSIUDLL+DSIUDLM in one
       halfword store. */
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadByte(addr) |
                                     (static_cast<uint16_t>(ReadByte(addr + 1u)) << 8));
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        WriteByte(addr, static_cast<uint8_t>(value & 0xFFu));
        WriteByte(addr + 1u, static_cast<uint8_t>(value >> 8));
    }

    void ResetCore() { Serial16550::Reset(); }

protected:
    uint32_t    RegStride() const override { return 1u; }
    const char* Name()      const override { return "DSIU"; }
    void SetInterruptLine(bool pending) override;
};
