#include <cerfcore/boards/board_context.h>

#include "nec_mobilepro_900_series_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class NecMobilepro900SeriesContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::NecMobilepro900Series; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilepro900SeriesContext, BoardContext);

}
