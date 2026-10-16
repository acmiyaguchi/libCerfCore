#include <cerfcore/boards/board_context.h>

#include "odo_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class OdoContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::Odo; }
};

}  /* namespace */

REGISTER_SERVICE_AS(OdoContext, BoardContext);

}
