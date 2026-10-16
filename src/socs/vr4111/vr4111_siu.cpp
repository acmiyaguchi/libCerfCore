#include "../vr41xx/vr41xx_siu_reset_csel.h"

#include <cerfcore/boards/board_context.h>
#include "vr4111_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

class Vr4111Siu : public Vr41xxSiuResetCsel {
public:
    using Vr41xxSiuResetCsel::Vr41xxSiuResetCsel;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Vr4111;
    }
};

}

REGISTER_SERVICE(Vr4111Siu);
