#include <cerfcore/boards/board_context.h>

#include "nec_sg2_vr5500_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class NecSg2Vr5500Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::NecSg2Vr5500; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecSg2Vr5500Context, BoardContext);

}
