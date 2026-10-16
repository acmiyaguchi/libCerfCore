#include "../mips_kseg_dram_rom_page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_cronos3e_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* DRAM space PA 0x00000000-0x07FFFFFF (VR4121 UM Table 6-6). */
constexpr uint32_t kDramVaBase = 0x80000000u;
constexpr uint32_t kDramPaBase = 0x00000000u;
constexpr uint32_t kDramSize   = 0x02000000u;

class NecCronos3ePageTableBuilder : public MipsKsegDramRomPageTableBuilder {
public:
    explicit NecCronos3ePageTableBuilder(CerfEmulator& emu)
        : MipsKsegDramRomPageTableBuilder(emu, {
              { kDramVaBase, kDramPaBase, kDramSize }, 0u, kDramSize,
          }) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }
};

}

REGISTER_SERVICE_AS(NecCronos3ePageTableBuilder, PageTableBuilder);

}
