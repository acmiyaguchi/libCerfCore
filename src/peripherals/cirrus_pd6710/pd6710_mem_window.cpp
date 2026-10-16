#include "pd6710_controller.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/samsung_smdk2410/samsung_smdk2410_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x10000000u;
constexpr uint32_t kSize = 0x01000000u;

class Pd6710MemWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    void     WriteByte (uint32_t addr, uint8_t  value) override;
    void     WriteHalf (uint32_t addr, uint16_t value) override;
};

}  /* namespace */

uint8_t Pd6710MemWindow::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    auto& ctl = emu_.Get<Pd6710Controller>();
    uint32_t card_addr;
    bool attribute, writable;
    if (!ctl.MapMem(off, &card_addr, &attribute, &writable)) {
        LOG(Pcmcia, "[MemWin] read8 +0x%X (no window) -> 0\n", off);
        return 0u;
    }
    if (attribute) return ctl.Slot().ReadAttribute8(card_addr);
    return ctl.Slot().ReadCommon8(card_addr);
}

uint16_t Pd6710MemWindow::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    auto& ctl = emu_.Get<Pd6710Controller>();
    uint32_t card_addr;
    bool attribute, writable;
    if (!ctl.MapMem(off, &card_addr, &attribute, &writable)) {
        LOG(Pcmcia, "[MemWin] read16 +0x%X (no window) -> 0\n", off);
        return 0u;
    }
    if (attribute) {
        /* Attribute memory drives even bytes only; the controller
           images the even byte on both halves of the 16-bit bus. */
        const uint8_t v = ctl.Slot().ReadAttribute8(card_addr & ~1u);
        return static_cast<uint16_t>((v << 8) | v);
    }
    return ctl.Slot().ReadCommon16(card_addr);
}

void Pd6710MemWindow::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kBase;
    auto& ctl = emu_.Get<Pd6710Controller>();
    uint32_t card_addr;
    bool attribute, writable;
    if (!ctl.MapMem(off, &card_addr, &attribute, &writable)) {
        LOG(Pcmcia, "[MemWin] write8 +0x%X = 0x%02X (no window)\n",
            off, value);
        return;
    }
    if (!writable) {
        LOG(Pcmcia, "[MemWin] write8 +0x%X = 0x%02X (window write-"
                "protected)\n", off, value);
        return;
    }
    if (attribute) ctl.Slot().WriteAttribute8(card_addr, value);
    else           ctl.Slot().WriteCommon8(card_addr, value);
}

void Pd6710MemWindow::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kBase;
    auto& ctl = emu_.Get<Pd6710Controller>();
    uint32_t card_addr;
    bool attribute, writable;
    if (!ctl.MapMem(off, &card_addr, &attribute, &writable)) {
        LOG(Pcmcia, "[MemWin] write16 +0x%X = 0x%04X (no window)\n",
            off, value);
        return;
    }
    if (!writable) {
        LOG(Pcmcia, "[MemWin] write16 +0x%X = 0x%04X (window write-"
                "protected)\n", off, value);
        return;
    }
    if (attribute) {
        /* Even byte carries the attribute data on a 16-bit write. */
        ctl.Slot().WriteAttribute8(card_addr & ~1u,
                                   static_cast<uint8_t>(value & 0xFFu));
        return;
    }
    ctl.Slot().WriteCommon16(card_addr, value);
}

REGISTER_SERVICE(Pd6710MemWindow);

}
