#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* HP Jornada 720 internal V.90 modem ASIC on SA-1110 CS1, modelled absent.
   Float 0xFF makes mctl.dll's presence probe (sub_F1992C: ID byte at +0x48
   must read 0x76) take its designed no-modem failure path. */
class HpJornada700Modem : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x08000000u; }
    uint32_t MmioSize() const override { return 0x00400000u; }

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }

    void WriteByte(uint32_t, uint8_t)  override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

}  /* namespace */

REGISTER_SERVICE(HpJornada700Modem);
