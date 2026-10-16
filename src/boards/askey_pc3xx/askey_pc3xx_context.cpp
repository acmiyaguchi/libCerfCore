#include <cerfcore/boards/board_context.h>

#include "askey_pc3xx_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class AskeyPc3xxContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::AskeyPc3xx; }
};

}  /* namespace */

REGISTER_SERVICE_AS(AskeyPc3xxContext, BoardContext);
