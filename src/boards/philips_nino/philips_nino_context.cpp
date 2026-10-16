#include <cerfcore/boards/board_context.h>

#include "philips_nino_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

class PhilipsNinoContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardFamilyId() const override { return BoardFamilyId::PhilipsNino; }

    /* CE 2.01 R3000 kernel masks mapped PFNs to 31 bits, so the default
       0xF0000000 window is unreachable; 0x20000000 is bit-31-clear and in the
       TX3912 undecoded span (system CS ends 0x1FFFFFFF, kuseg aliases 0x40000000+). */
    uint32_t GuestAdditionsWindowBase() const override { return 0x20000000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsNinoContext, BoardContext);

}
