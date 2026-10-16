#include "../mips_kseg_dram_rom_page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "casio_jx670_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* DRAM area PA 0x00000000-0x07FFFFFF (VR4131 UM Fig 3-1); ROMHDR
   ulRAMStart 0x80051000 / ulRAMEnd 0x81000000 place this board's populated
   DRAM at kseg0 0x80000000, 16 MB. */
constexpr uint32_t kDramVaBase = 0x80000000u;
constexpr uint32_t kDramPaBase = 0x00000000u;
constexpr uint32_t kDramSize   = 0x01000000u;

class CasioJx670PageTableBuilder : public MipsKsegDramRomPageTableBuilder {
public:
    explicit CasioJx670PageTableBuilder(CerfEmulator& emu)
        : MipsKsegDramRomPageTableBuilder(emu, {
              { kDramVaBase, kDramPaBase, kDramSize }, 0u, kDramSize,
          }) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioJx670;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioJx670PageTableBuilder, PageTableBuilder);

}
