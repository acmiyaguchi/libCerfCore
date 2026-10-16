#include <cerfcore/boards/board_context.h>

#include "hp_jornada_700_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class HpJornada700Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::HpJornada700; }
};

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada700Context, BoardContext);
