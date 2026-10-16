#include "../../peripherals/ti_tps65950/tps65950_board.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../socs/omap3530/omap3530_gpio_bus.h"
#include "../../socs/omap3530/omap3530_sys_nirq_pad.h"
#include "../../socs/omap3530/omap3530_sysc_padconf.h"

#include <optional>
#include <cerfcore/boards/board_context.h>
#include "ti_omap35x_evm_family_id.h"

namespace {

class TiOmap35xEvmTps65950Board : public Tps65950Board {
public:
    using Tps65950Board::Tps65950Board;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
    }

    uint64_t HfclkinHz() const override { return 26000000u; }

    /* TI SWCU056C section 3.1.4 (page 10): "BOOT0 pin is tied to 1. BOOT1 pin is tied to ground." */
    Tps65950BootStraps BootStraps() const override { return {true, false}; }

    void DriveInt1(bool asserted) override { emu_.Get<Omap3530SysNirqPad>().Drive(asserted); }

    /* SPRUF98Y Table 7-4 (page 775): CONTROL_PADCONF_GPMC_WAIT2[15:0] mode 0 gpmc_wait2, mode 4 gpio_64, mode 7
       safe_mode; TPS65950 SWCS032F Table 3-2 (page 13): MSECURE has no internal pull. */
    bool MsecureHigh() override {
        auto&          pads = emu_.Get<Omap3530SyscPadconf>();
        const uint32_t mode = pads.PadMuxMode(kPadGpmcWait2);
        if (mode == kModeGpio64) {
            if (const std::optional<bool> level = emu_.Get<Omap3530GpioBus>().DrivenLevel(kGpio64)) return *level;
        } else if (mode != kModeGpmcWait2 && mode != kModeSafe) {
            emu_.Get<Fatal>().Die("TiOmap35xEvmTps65950Board: MSECURE ball K8 in pad mode %u, a mode Table 7-4 leaves empty",
                                  mode);
        }
        switch (pads.PadPullOf(kPadGpmcWait2)) {
            case Omap3530SyscPadconf::PadPull::Up:   return true;
            case Omap3530SyscPadconf::PadPull::Down: return false;
            case Omap3530SyscPadconf::PadPull::None: break;
        }
        emu_.Get<Fatal>().Die("TiOmap35xEvmTps65950Board: MSECURE floats, ball K8 drives nothing and has no pull");
    }

private:
    static constexpr uint32_t kPadGpmcWait2  = 0x0D0u;
    static constexpr uint32_t kModeGpmcWait2 = 0u;
    static constexpr uint32_t kModeGpio64    = 4u;
    static constexpr uint32_t kModeSafe      = 7u;
    static constexpr uint32_t kGpio64        = 64u;
};

}

REGISTER_SERVICE_AS(TiOmap35xEvmTps65950Board, Tps65950Board);
