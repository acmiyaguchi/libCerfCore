#include "../../peripherals/mediaq_mq200/mediaq_mq200_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "pangolin_family_id.h"

namespace cerfcore {

namespace {

class PangolinMq200Config : public MediaQMq200Config {
public:
    using MediaQMq200Config::MediaQMq200Config;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Pangolin;
    }

    uint32_t HostWindowBase() const override { return 0x4B800000u; }

    /* smartbook_g138_ce4_1 ddi_mq200.dll (MHz, PLL word) table at 0x23F9F88. */
    uint32_t OscillatorHz() const override { return 12288000u; }

    uint32_t BusInterfaceMode() const override { return 0x08u; }
};

}

REGISTER_SERVICE_AS(PangolinMq200Config, MediaQMq200Config);

}
