#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/pci/pci_host_bridge.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../cpu/vr5500/vr5500_id.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

/* The VRC5477 external PCI memory window (PA 0x08000000, 128 MB; BSP_REG_PA_PCI_MEM).
   Carries both config cycles and BAR memory; forwards every access to the host
   bridge, which decodes config-vs-memory from PCIINIT00 and routes to PCI devices. */

namespace {

constexpr uint32_t kPciMemBase = 0x08000000u;
constexpr uint32_t kPciMemSize = 0x08000000u;

class Vrc5477PciWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Vr5500;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kPciMemBase; }
    uint32_t MmioSize() const override { return kPciMemSize; }

    uint8_t  ReadByte (uint32_t a) override { return static_cast<uint8_t>(Bridge().WindowRead(a, 1)); }
    uint16_t ReadHalf (uint32_t a) override { return static_cast<uint16_t>(Bridge().WindowRead(a, 2)); }
    uint32_t ReadWord (uint32_t a) override { return Bridge().WindowRead(a, 4); }
    void WriteByte(uint32_t a, uint8_t  v) override { Bridge().WindowWrite(a, v, 1); }
    void WriteHalf(uint32_t a, uint16_t v) override { Bridge().WindowWrite(a, v, 2); }
    void WriteWord(uint32_t a, uint32_t v) override { Bridge().WindowWrite(a, v, 4); }

    /* MIPS 64-bit ld/sd to the linear framebuffer = two little-endian 32-bit halves. */
    uint64_t ReadDword(uint32_t a) override {
        return Bridge().WindowRead(a, 4) |
               (static_cast<uint64_t>(Bridge().WindowRead(a + 4, 4)) << 32);
    }
    void WriteDword(uint32_t a, uint64_t v) override {
        Bridge().WindowWrite(a, static_cast<uint32_t>(v), 4);
        Bridge().WindowWrite(a + 4, static_cast<uint32_t>(v >> 32), 4);
    }

    /* The enumerated PCI Peripheral is the hibernation entry point for the
       (non-Peripheral) bridge + its devices. */
    void SaveState(StateWriter& w) override    { Bridge().SaveState(w); }
    void RestoreState(StateReader& r) override { Bridge().RestoreState(r); }
    void PostRestore() override                { Bridge().PostRestore(); }

private:
    PciHostBridge& Bridge() { return emu_.Get<PciHostBridge>(); }
};

REGISTER_SERVICE(Vrc5477PciWindow);

}  /* namespace */

}
