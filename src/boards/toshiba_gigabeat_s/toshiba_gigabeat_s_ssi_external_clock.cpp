#include "../../socs/freescale_ssi_external_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "toshiba_gigabeat_s_family_id.h"

namespace {

class ToshibaGigabeatSSsiExternalClock : public FreescaleSsiExternalClock {
public:
    using FreescaleSsiExternalClock::FreescaleSsiExternalClock;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
    }

    /* zune_keel wavedev_wm8978.dll sub_30E1D4C writes WM8978 register 6 only as sub_30E181C(6, 0, 480), bits 8:5,
       and leaves bit 0, which the Linux WM8978 driver (wm8978_set_dai_fmt) sets when the codec drives the bit
       clock and frame sync. */
    FreescaleAudioFormat Format(uint32_t) const override { return FreescaleAudioFormat{}; }
    uint32_t             FrameSyncBitClocks(uint32_t) const override { return 0u; }
};

}

REGISTER_SERVICE_AS(ToshibaGigabeatSSsiExternalClock, FreescaleSsiExternalClock);
