#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "philips_nino_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Table 4.2.1 (TMPR3911.pdf PDF p.103) decodes PA $0200_0000, 32 MB, as DRAM
   BANK 1 while ENCS1DRAM is clear. The Nino fits no chips on that bank, and the
   OAL leaves it enabled (MEM_CONFIG4 ENBANK1HDRAM, ENRFSH1) until it probes it,
   so the controller drives a normal DRAM cycle onto an undriven data bus. */
constexpr uint32_t kBank1Base = 0x02000000u;
constexpr uint32_t kBank1Size = 0x02000000u;

class PhilipsNinoEmptyDramBank1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsNino;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBank1Base; }
    uint32_t MmioSize() const override { return kBank1Size; }

    uint8_t  ReadByte (uint32_t addr) override { NoteFloat(addr); return 0xFFu; }
    uint16_t ReadHalf (uint32_t addr) override { NoteFloat(addr); return 0xFFFFu; }
    uint32_t ReadWord (uint32_t addr) override { NoteFloat(addr); return 0xFFFFFFFFu; }
    uint64_t ReadDword(uint32_t addr) override { NoteFloat(addr); return 0xFFFFFFFFFFFFFFFFull; }

    /* No memory cell backs the cycle, so the datum is lost. nk.exe sub_9F4117B4
       writes 0xAAAA5555 here, then 0xFFFFFFFF one word up, and reads the first
       word back: an absent bank cannot return the pattern. */
    void WriteByte (uint32_t, uint8_t)  override {}
    void WriteHalf (uint32_t, uint16_t) override {}
    void WriteWord (uint32_t, uint32_t) override {}
    void WriteDword(uint32_t, uint64_t) override {}

private:
    void NoteFloat(uint32_t addr) {
        if (floats_++ == 0u) {
            LOG(Mem, "PhilipsNinoEmptyDramBank1: read of unpopulated DRAM BANK 1 "
                    "at pa=0x%08X returns an undriven bus\n", addr);
        }
    }

    uint32_t floats_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(PhilipsNinoEmptyDramBank1);

}
