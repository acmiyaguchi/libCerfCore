#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace cerfcore {

namespace {

/* Pre-MMU shadow of the DRAM VA window (PA 0x88000000): the CE 2.11 kernel's
   softfloat delay (nk.exe sub_80038FF0) touches its FP-flags global at VA
   0x88081220 before the MMU maps 0x88000000 to DRAM (0xC0000000). SA-1100
   reserved PCM space absorbs that pre-MMU access; without it the boot faults. */
class HpJornada820PreMmuDramWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada820;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x88000000u; }
    uint32_t MmioSize() const override { return 0x01000000u; }

    uint8_t  ReadByte(uint32_t) override { return 0u; }
    uint16_t ReadHalf(uint32_t) override { return 0u; }
    uint32_t ReadWord(uint32_t) override { return 0u; }

    void WriteByte(uint32_t, uint8_t)  override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

}  /* namespace */

REGISTER_SERVICE(HpJornada820PreMmuDramWindow);

}
