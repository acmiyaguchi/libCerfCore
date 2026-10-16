#include <cerfcore/boards/board_context.h>

#include "siemens_simatic_tp177b_4_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class SiemensSimaticTp177b4Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::SiemensSimaticTp177b4; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimaticTp177b4Context, BoardContext);
