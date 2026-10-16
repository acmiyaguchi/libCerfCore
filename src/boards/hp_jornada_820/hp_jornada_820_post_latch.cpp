#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* Jornada 820 board POST/progress latch on SA-1100 nCS2 (PA 0x12000000).
   The firmware writes one-byte boot-progress codes to +0x400 (nk.exe
   sub_80039A04 steps 0..0xEF; the boot-fail spin at 0x80001074 writes 0xE)
   and never reads it back, so reads float 0xFF (absent board bus). */
class HpJornada820PostLatch : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada820;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x12000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }

    void WriteByte(uint32_t addr, uint8_t  v) override { LogProgress(addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { LogProgress(addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { LogProgress(addr, v); }

private:
    void LogProgress(uint32_t addr, uint32_t v) {
        if (addr - MmioBase() != 0x400u) return;
        LOG(Boot, "[J820] firmware boot-progress code 0x%02X\n", v & 0xFFu);
    }
};

}  /* namespace */

REGISTER_SERVICE(HpJornada820PostLatch);
