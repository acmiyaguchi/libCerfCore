#include "../armv7a_coproc_provider_base.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/imx51/imx51_id.h"
#include "../../socs/omap3530/omap3530_id.h"

namespace cerfcore {

namespace {

class CortexA8CoprocProvider : public Armv7aCoprocProviderBase {
public:
    using Armv7aCoprocProviderBase::Armv7aCoprocProviderBase;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Omap3530 || soc == SocId::Imx51;
    }
};

}

REGISTER_SERVICE_AS(CortexA8CoprocProvider, ArmCoprocProvider);

}
