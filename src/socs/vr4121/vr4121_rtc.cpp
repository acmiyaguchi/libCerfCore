#include "../vr41xx/vr41xx_rtc.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "vr4121_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class Vr4121Rtc : public Vr41xxRtc {
public:
    using Vr41xxRtc::Vr41xxRtc;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Vr4121;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4121Rtc, Vr41xxRtc);

}
