#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../serial/serial_16550.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../tracing/kernel_debug_sink.h"
#include "../peripheral_dispatcher.h"

#include <cstdint>
#include <string>

/* Memory-mapped 16550: registers sit at reg-index*RegStride (PXA255 =4, VR4102 SIU =1,
   a 16-bit-bus part =2). */
class Uart16550 : public Peripheral, public Serial16550 {
public:
    Uart16550(CerfEmulator& emu, Config cfg)
        : Peripheral(emu),
          Serial16550(nullptr, [this](bool pending) { SetInterruptLine(pending); }, cfg) {}

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioSize() const override { return 0x00001000u; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t idx = (addr - MmioBase()) / RegStride();
        if (idx < 8u) return ReadReg8(idx);
        return ReadExtReg(idx);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t idx = (addr - MmioBase()) / RegStride();
        if (idx < 8u) { WriteReg8(idx, static_cast<uint8_t>(value & 0xFFu)); return; }
        WriteExtReg(idx, value);
    }

    /* 16550 registers are byte-wide; CE writes THR / reads LSR with STRB / LDRB.
       At word stride a byte access maps onto the aligned word register; the
       in-between byte lanes are the register's zero upper bytes. */
    uint8_t ReadByte(uint32_t addr) override {
        if ((addr - MmioBase()) % RegStride() != 0u) return 0u;
        return static_cast<uint8_t>(ReadWord(addr) & 0xFFu);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        if ((addr - MmioBase()) % RegStride() != 0u) return;
        WriteWord(addr, value);
    }

    void SaveState(StateWriter& w) override    { Serial16550::SaveState(w); }
    void RestoreState(StateReader& r) override { Serial16550::RestoreState(r); }
    void PostRestore() override                { PostRestoreLine(); }

protected:
    virtual uint32_t    RegStride() const = 0;
    virtual const char* Name()      const = 0;

    /* Drive (pending=true) or clear the UART's INTR pin; the concrete routes it to its
       controller (PXA255 INTC source, VR4102 ICU, board GPIO, …). */
    virtual void SetInterruptLine(bool pending) = 0;

    /* Chip registers beyond the 8 standard ones (PXA255 ISR at index 8, VR4102
       SIUIRSEL at index 8). Default: unsupported access halts. */
    virtual uint32_t ReadExtReg(uint32_t idx) {
        HaltUnsupportedAccess("ReadWord", MmioBase() + idx * RegStride(), 0);
    }
    virtual void WriteExtReg(uint32_t idx, uint32_t value) {
        HaltUnsupportedAccess("WriteWord", MmioBase() + idx * RegStride(), value);
    }

    /* With no personality attached the port is the board's debug console. */
    void DeliverTx(uint8_t byte) override {
        if (DeliverTxToEndpoint(byte)) return;
        emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(byte), tx_line_, Name());
    }

private:
    std::string tx_line_;   /* host-side console accumulator, not guest state */
};
