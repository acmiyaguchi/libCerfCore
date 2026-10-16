#include <cerfcore/boards/board_context.h>

#include "siemens_simatic_mp377_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class SiemensSimaticMp377Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::SiemensSimaticMp377; }

    uint32_t GuestAdditionsWindowBase() const override { return 0xE0000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimaticMp377Context, BoardContext);

}
