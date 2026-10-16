#include "../../socs/omap3530/omap3530_mcbsp_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/ti_tps65950/tps65950_audio_interface.h"
#include <cerfcore/boards/board_context.h>
#include "ti_omap35x_evm_family_id.h"

namespace cerfcore {

namespace {

class TiOmap35xEvmMcBspWiring : public Omap3530McBspWiring {
public:
    using Omap3530McBspWiring::Omap3530McBspWiring;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
    }

    Omap3530McBspPeer* Peer(uint32_t port) override {
        return port == 2u ? &emu_.Get<Tps65950AudioInterface>() : nullptr;
    }

    Omap3530McBspClockPad* ClksPad() override { return &emu_.Get<Tps65950AudioInterface>(); }
};

}

REGISTER_SERVICE_AS(TiOmap35xEvmMcBspWiring, Omap3530McBspWiring);

}
