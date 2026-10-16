#include "pd6710_controller.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/samsung_smdk2410/samsung_smdk2410_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"
#include "../pcmcia/pcmcia_slot.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x11000000u;
constexpr uint32_t kSize = 0x00010000u;

constexpr uint32_t kPcicIndexOffset = 0x3E0u;
constexpr uint32_t kPcicDataOffset  = 0x3E1u;

class Pd6710IoWindow : public Peripheral {
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

    /* The single forward site for the Cirrus slot (a Service off the walk);
       the mem-window peripheral deliberately does not duplicate it. */
    void SaveState(StateWriter& w) override {
        emu_.Get<Pd6710Controller>().SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        emu_.Get<Pd6710Controller>().RestoreState(r);
    }
    void PostRestore() override {
        emu_.Get<Pd6710Controller>().PostRestore();
    }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    void     WriteByte (uint32_t addr, uint8_t  value) override;
    void     WriteHalf (uint32_t addr, uint16_t value) override;
};

}  /* namespace */

uint8_t Pd6710IoWindow::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    auto& ctl = emu_.Get<Pd6710Controller>();
    if (off == kPcicIndexOffset || off == kPcicDataOffset) {
        return ctl.ReadPcicByte(off - kPcicIndexOffset);
    }
    uint32_t card_io;
    if (!ctl.MapIo(off, &card_io)) {
        LOG(Pcmcia, "[IoWin] read8 +0x%X (no window) -> 0\n", off);
        return 0u;
    }
    return ctl.Slot().ReadIo8(card_io);
}

uint16_t Pd6710IoWindow::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    auto& ctl = emu_.Get<Pd6710Controller>();
    if (off == kPcicIndexOffset || off == kPcicDataOffset) {
        HaltUnsupportedAccess("ReadHalf", addr, 0);
    }
    uint32_t card_io;
    if (!ctl.MapIo(off, &card_io)) {
        LOG(Pcmcia, "[IoWin] read16 +0x%X (no window) -> 0\n", off);
        return 0u;
    }
    return ctl.Slot().ReadIo16(card_io);
}

void Pd6710IoWindow::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kBase;
    auto& ctl = emu_.Get<Pd6710Controller>();
    if (off == kPcicIndexOffset || off == kPcicDataOffset) {
        ctl.WritePcicByte(off - kPcicIndexOffset, value);
        return;
    }
    uint32_t card_io;
    if (!ctl.MapIo(off, &card_io)) {
        LOG(Pcmcia, "[IoWin] write8 +0x%X = 0x%02X (no window)\n", off, value);
        return;
    }
    ctl.Slot().WriteIo8(card_io, value);
}

void Pd6710IoWindow::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kBase;
    auto& ctl = emu_.Get<Pd6710Controller>();
    if (off == kPcicIndexOffset || off == kPcicDataOffset) {
        HaltUnsupportedAccess("WriteHalf", addr, value);
    }
    uint32_t card_io;
    if (!ctl.MapIo(off, &card_io)) {
        LOG(Pcmcia, "[IoWin] write16 +0x%X = 0x%04X (no window)\n",
            off, value);
        return;
    }
    ctl.Slot().WriteIo16(card_io, value);
}

REGISTER_SERVICE(Pd6710IoWindow);

}
