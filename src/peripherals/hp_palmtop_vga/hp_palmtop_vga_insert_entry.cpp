#include "hp_palmtop_vga_insert_entry.h"

#include "hp_palmtop_vga_card.h"
#include <cerfcore/core/cerf_emulator.h>

#include <memory>

namespace cerfcore {

REGISTER_SERVICE(HpPalmtopVgaInsertEntry);

SlotEntry HpPalmtopVgaInsertEntry::BuildInsertEntry(PcmciaCardCatalog::CardInserter inserter) {
    SlotEntry entry;
    entry.insert = [this, inserter] {
        inserter(std::make_unique<HpPalmtopVgaCard>(emu_));
    };
    entry.details = {
        WidgetControl{InfoControl{"External VGA monitor output, in its own window."}},
        WidgetControl{InfoControl{"Works on the HP Jornada series."}},
        WidgetControl{InfoControl{"Verified on the HP Jornada 720."}},
    };
    return entry;
}

}
