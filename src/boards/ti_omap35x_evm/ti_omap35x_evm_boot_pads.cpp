#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../socs/omap3530/omap3530_sysc_padconf.h"
#include <cerfcore/boards/board_context.h>
#include "ti_omap35x_evm_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* SPRUF98Y Table 7-4 (page 775): CONTROL_PADCONF_GPMC_WAIT2[15:0] at 0x4800 20D0. */
constexpr uint32_t kPadGpmcWait2 = 0x480020D0u;

/* SPRUF98Y Table 7-75 (page 867): INPUTENABLE0 [8], PULLTYPESELECT0 [4], PULLUDENABLE0 [3], MUXMODE0 [2:0]. */
constexpr uint16_t kInputEnable = 1u << 8;
constexpr uint16_t kPullUp      = 1u << 4;
constexpr uint16_t kPullEnable  = 1u << 3;
constexpr uint16_t kModeGpio64  = 4u;

constexpr uint16_t kBootGpmcWait2 = kInputEnable | kPullUp | kPullEnable | kModeGpio64;

class TiOmap35xEvmBootPads : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm; }

    void OnReady() override {
        Seed();
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { Seed(); });
    }

private:
    void Seed() { emu_.Get<Omap3530SyscPadconf>().WriteHalf(kPadGpmcWait2, kBootGpmcWait2); }
};

}

REGISTER_SERVICE(TiOmap35xEvmBootPads);

}
