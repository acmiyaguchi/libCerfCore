#include <cerfcore/boards/board_context.h>

#include "pangolin_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class PangolinContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::Pangolin; }
};

}  /* namespace */

REGISTER_SERVICE_AS(PangolinContext, BoardContext);

}
