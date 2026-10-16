#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/pci/pci_host_bridge.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../cpu/vr5500/vr5500_id.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>


namespace {

constexpr uint32_t kPciIoPaBase = 0x14000000u;   /* BSP_REG_PA_PCI_IO */
constexpr uint32_t kWindowBase  = 0x14001000u;   /* above the M1535 legacy 4 KB */
constexpr uint32_t kWindowSize  = 0x01FFF000u;   /* to 0x16000000 (BSP_REG_PCI_IOSIZE=0x02000000) */

class Vrc5477PciIoWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Vr5500;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kWindowBase; }
    uint32_t MmioSize() const override { return kWindowSize; }

    uint8_t  ReadByte (uint32_t a) override { return static_cast<uint8_t>(Bridge().WindowIoRead(Io(a), 1)); }
    uint16_t ReadHalf (uint32_t a) override { return static_cast<uint16_t>(Bridge().WindowIoRead(Io(a), 2)); }
    uint32_t ReadWord (uint32_t a) override { return Bridge().WindowIoRead(Io(a), 4); }
    void WriteByte(uint32_t a, uint8_t  v) override { Bridge().WindowIoWrite(Io(a), v, 1); }
    void WriteHalf(uint32_t a, uint16_t v) override { Bridge().WindowIoWrite(Io(a), v, 2); }
    void WriteWord(uint32_t a, uint32_t v) override { Bridge().WindowIoWrite(Io(a), v, 4); }

private:
    static uint32_t Io(uint32_t pa) { return pa - kPciIoPaBase; }
    PciHostBridge& Bridge() { return emu_.Get<PciHostBridge>(); }
};

REGISTER_SERVICE(Vrc5477PciIoWindow);

}  /* namespace */
