#include "boot_mode.h"

#include <cerfcore/boot/rom_parser_service.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../boards/page_table_builder.h"

namespace cerfcore {

namespace {

class XipPlaceBoot : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetRomPlacingMode()
            == RomPlacingMode::FlatContainer;
    }

    /* ARM ARM DDI 0406C.c A2.3.2 BXWritePC(), p. A2-47: "BranchTo(address<31:1>
       :'0')" - bit<0> is the instruction-set selector, not part of the
       address. */
    uint32_t ColdEntryPa() override {
        auto& rom = emu_.Get<RomParserService>();
        return emu_.Get<PageTableBuilder>().VaToPa(rom.EntryVa() & 0xFFFFFFFEu);
    }

    /* ARM ARM DDI 0406C.c A2.3.2 BXWritePC(), p. A2-47: "if address<0> == '1'
       then SelectInstrSet(InstrSet_Thumb)". */
    bool ColdEntryThumb() override {
        return (emu_.Get<RomParserService>().EntryVa() & 1u) != 0u;
    }

    uint32_t ColdStackPa() override {
        return emu_.Get<PageTableBuilder>().InitStackTopPa();
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(XipPlaceBoot, BootMode);

}
