#include <cerfcore/boards/board_context.h>

#include "nec_cronos3e_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class NecCronos3eContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::NecCronos3e; }
};

}

REGISTER_SERVICE_AS(NecCronos3eContext, BoardContext);
