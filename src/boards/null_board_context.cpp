#include <cerfcore/boards/board_context.h>

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class NullBoardContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    bool ShouldRegister() override { return true; }

    std::string_view GetBoardFamilyId() const override { return {}; }
};

}

REGISTER_SERVICE_AS_FALLBACK(NullBoardContext, BoardContext);

}
