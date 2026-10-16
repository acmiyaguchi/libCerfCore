#include <cerfcore/boards/board_context.h>

#include "compaq_ipaq_h3000_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class CompaqIpaqH3000Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::CompaqIpaqH3000; }
};

}  /* namespace */

REGISTER_SERVICE_AS(CompaqIpaqH3000Context, BoardContext);
