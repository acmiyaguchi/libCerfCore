#include "nec_mobilepro_900_series_boot_args.h"

#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"
#include <cerfcore/boot/rom_parser_queries.h>
#include <cerfcore/core/cerf_emulator.h>

bool NecMobilepro900SeriesBootArgs::BoardMatchesKernelMajor(uint16_t major) const {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd || bd->GetBoardFamilyId() != BoardFamilyId::NecMobilepro900Series) return false;
    uint16_t maj = 0, min = 0;
    return emu_.Get<RomParserQueries>().KernelSubsystemVersion(maj, min) && maj == major;
}
