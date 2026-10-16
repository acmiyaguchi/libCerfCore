#include "symbol_hamptons_display.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "symbol_mk500_a035qn02_id.h"

namespace cerfcore {

namespace {

class SymbolHamptonsA035qn02Display : public SymbolHamptonsDisplay {
public:
    using SymbolHamptonsDisplay::SymbolHamptonsDisplay;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardId() == BoardId::SymbolMk500A035qn02;
    }

    uint32_t DisplayId() const override { return 0x113u; }
};

}  // namespace

REGISTER_SERVICE_AS(SymbolHamptonsA035qn02Display, SymbolHamptonsDisplay);

}
