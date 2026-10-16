#include "page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <typeinfo>

DramRegion PageTableBuilder::EntryXipRomRegion() const {
    emu_.Get<Fatal>().Die("%s declares no entry-XIP ROM region", typeid(*this).name());
}
