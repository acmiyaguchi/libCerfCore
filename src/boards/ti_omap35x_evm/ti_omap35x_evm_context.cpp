#include <cerfcore/boards/board_context.h>

#include "ti_omap35x_evm_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class TiOmap35xEvmContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::TiOmap35xEvm; }
};

}  /* namespace */

REGISTER_SERVICE_AS(TiOmap35xEvmContext, BoardContext);

}
