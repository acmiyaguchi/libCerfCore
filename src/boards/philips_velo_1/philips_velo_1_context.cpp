#include <cerfcore/boards/board_context.h>

#include "philips_velo_1_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class PhilipsVelo1Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::PhilipsVelo1; }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1Context, BoardContext);
