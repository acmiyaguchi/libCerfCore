#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* TX39 CS1 slot 0x10000000, spanning to CS2 at 0x10400000; unpopulated on the
   HC-4100. nk.exe @0x91023A98 compares CS1[0] against 0 and jalrs pointers from
   CS1+0x20 on a match (@0x91023BC0), so reads MUST be all-ones - a 0 read matches
   the probe and the guest jumps into the empty slot. */
constexpr uint32_t kCs1Base = 0x10000000u;
constexpr uint32_t kCs1Size = 0x00400000u;

class SharpMobilonHcEmptyCs1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kCs1Base; }
    uint32_t MmioSize() const override { return kCs1Size; }

    uint8_t  ReadByte (uint32_t addr) override { NoteFloat(addr); return 0xFFu; }
    uint16_t ReadHalf (uint32_t addr) override { NoteFloat(addr); return 0xFFFFu; }
    uint32_t ReadWord (uint32_t addr) override { NoteFloat(addr); return 0xFFFFFFFFu; }
    uint64_t ReadDword(uint32_t addr) override { NoteFloat(addr); return 0xFFFFFFFFFFFFFFFFull; }

    void WriteByte (uint32_t, uint8_t)  override {}
    void WriteHalf (uint32_t, uint16_t) override {}
    void WriteWord (uint32_t, uint32_t) override {}
    void WriteDword(uint32_t, uint64_t) override {}

private:
    void NoteFloat(uint32_t addr) {
        if (floats_++ == 0u) {
            LOG(Mem, "SharpMobilonHcEmptyCs1: read of unpopulated CS1 slot "
                    "at pa=0x%08X returns an undriven bus\n", addr);
        }
    }

    uint32_t floats_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(SharpMobilonHcEmptyCs1);

}
