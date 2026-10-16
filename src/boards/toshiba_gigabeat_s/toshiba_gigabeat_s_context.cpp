#include <cerfcore/boards/board_context.h>

#include "toshiba_gigabeat_s_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class ToshibaGigabeatSContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::ToshibaGigabeatS; }
};

}  /* namespace */

REGISTER_SERVICE_AS(ToshibaGigabeatSContext, BoardContext);
