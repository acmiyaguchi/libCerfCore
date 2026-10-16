#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* Absent Jornada 720 debug board (CL-CD1284) at PA 0x1A000000: reads
   float 0xFF, writes drop. The range must stay mapped - KernelStart
   writes boot-progress codes to +0x400 and the PCMCIA driver strobes
   its power latch there; an unmapped range FATALs the boot. */
class HpJornada700DebugBoard : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x1A000000u; }
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
        if ((v & 0xFF00u) == 0x7000u) {
            /* The PCMCIA driver strobes 0x70n0/0x70nF command pairs
               here on socket power changes. */
            LOG(Pcmcia, "[J720] PCMCIA power latch 0x%04X\n", v & 0xFFFFu);
            return;
        }
        LOG(Boot, "[J720] firmware boot-progress code 0x%02X\n", v & 0xFFu);
    }
};

}  /* namespace */

REGISTER_SERVICE(HpJornada700DebugBoard);
