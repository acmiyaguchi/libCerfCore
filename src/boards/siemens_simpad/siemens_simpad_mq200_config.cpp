#include "../../peripherals/mediaq_mq200/mediaq_mq200_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"

namespace cerfcore {

namespace {

class SiemensSimpadMq200Config : public MediaQMq200Config {
public:
    using MediaQMq200Config::MediaQMq200Config;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    uint32_t HostWindowBase() const override { return 0x4B800000u; }

    /* simpad_sl4_ce4_10 ddi.dll dword_1353DE0 (MHz, PLL word) table, read by sub_134412C. */
    uint32_t OscillatorHz() const override { return 12288000u; }

    /* simpad_sl4_ce4_10 ddi.dll sub_1344254: (DC01R & 0x3F) == 8 selects the SA1110 PM00R. */
    uint32_t BusInterfaceMode() const override { return 0x08u; }
};

}

REGISTER_SERVICE_AS(SiemensSimpadMq200Config, MediaQMq200Config);

}
