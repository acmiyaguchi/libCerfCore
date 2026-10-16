#include <cerfcore/boards/board_context.h>

#include "ford_sync_gen2_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class FordSyncGen2Context : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::FordSyncGen2; }
};

}  /* namespace */

REGISTER_SERVICE_AS(FordSyncGen2Context, BoardContext);
