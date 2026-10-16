#include "../mips_kseg_dram_rom_page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kDramVaBase = 0x80000000u;
constexpr uint32_t kDramPaBase = 0x00000000u;
constexpr uint32_t kDramSize   = 0x00800000u;   /* 8 MB (ROMHDR ulRAMEnd) */

constexpr uint32_t kRomVaBase  = 0x9F000000u;   /* ROMHDR physfirst (kseg0) */
constexpr uint32_t kRomPaBase  = 0x1F000000u;   /* ROM space */
constexpr uint32_t kRomSize    = 0x01000000u;   /* 16 MB (nk.bin flat XIP) */

class NecMobilePro700PageTableBuilder : public MipsKsegDramRomPageTableBuilder {
public:
    explicit NecMobilePro700PageTableBuilder(CerfEmulator& emu)
        : MipsKsegDramRomPageTableBuilder(emu, {
              { kDramVaBase, kDramPaBase, kDramSize }, 0u, kDramSize,
          }, { kRomVaBase, kRomPaBase, kRomSize }) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro700PageTableBuilder, PageTableBuilder);

}
