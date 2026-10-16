#include <cerfcore/boards/board_context.h>

#include "hp_jornada_820_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class HpJornada820Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::HpJornada820; }
};

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada820Context, BoardContext);
