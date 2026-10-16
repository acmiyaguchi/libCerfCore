#include <cerfcore/boards/board_context.h>

#include "nokia_sea_ray_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class NokiaSeaRayContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::NokiaSeaRay; }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayContext, BoardContext);

}
