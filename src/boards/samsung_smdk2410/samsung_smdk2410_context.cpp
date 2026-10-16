#include <cerfcore/boards/board_context.h>

#include "samsung_smdk2410_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class SamsungSmdk2410Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::SamsungSmdk2410; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SamsungSmdk2410Context, BoardContext);
