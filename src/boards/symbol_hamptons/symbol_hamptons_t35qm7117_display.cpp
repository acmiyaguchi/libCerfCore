#include "symbol_hamptons_display.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "symbol_hamptons_family_id.h"

namespace cerfcore {

namespace {

class SymbolHamptonsT35qm7117Display : public SymbolHamptonsDisplay {
public:
    using SymbolHamptonsDisplay::SymbolHamptonsDisplay;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() ==
               BoardFamilyId::SymbolHamptons;
    }

    /* dump.bin ROM offset 0x00082008 = 0x114. */
    uint32_t DisplayId() const override { return 0x114u; }
};

}  // namespace

REGISTER_SERVICE_AS_FALLBACK(SymbolHamptonsT35qm7117Display, SymbolHamptonsDisplay);

}
