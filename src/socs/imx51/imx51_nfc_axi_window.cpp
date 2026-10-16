#include "imx51_nfc.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* NFC AXI register window page 1 (RM Table 45-4/45-5): spare buffers + the
   NAND_CMD/ADD/CFG1/LAUNCH registers. Page 0 (0xCFFF0000) is backed RAM. */
constexpr uint32_t kAxiWindowBase = 0xCFFF1000u;
constexpr uint32_t kAxiWindowSize = 0x00001000u;

class Imx51NfcAxiWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.TryGet<Imx51Nfc>() != nullptr;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kAxiWindowBase; }
    uint32_t MmioSize() const override { return kAxiWindowSize; }

    uint8_t  ReadByte (uint32_t a) override { return static_cast<uint8_t>(Nfc().NfcRead(a, 1)); }
    uint16_t ReadHalf (uint32_t a) override { return static_cast<uint16_t>(Nfc().NfcRead(a, 2)); }
    uint32_t ReadWord (uint32_t a) override { return Nfc().NfcRead(a, 4); }
    void WriteByte (uint32_t a, uint8_t  v) override { Nfc().NfcWrite(a, v, 1); }
    void WriteHalf (uint32_t a, uint16_t v) override { Nfc().NfcWrite(a, v, 2); }
    void WriteWord (uint32_t a, uint32_t v) override { Nfc().NfcWrite(a, v, 4); }

private:
    Imx51Nfc& Nfc() { return emu_.Get<Imx51Nfc>(); }
};

}  /* namespace */

REGISTER_SERVICE(Imx51NfcAxiWindow);

}
