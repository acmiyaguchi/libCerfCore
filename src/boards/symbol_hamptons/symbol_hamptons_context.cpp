#include <cerfcore/boards/board_context.h>

#include "symbol_hamptons_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class SymbolHamptonsContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::SymbolHamptons; }
};

}

REGISTER_SERVICE_AS(SymbolHamptonsContext, BoardContext);
