#include <cerfcore/boards/board_context.h>

#include "sharp_brain_pw_sh4_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class SharpBrainPwSh4Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::SharpBrainPwSh4; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4Context, BoardContext);

}
