#include "../freescale_uart_impl.h"
#include <cerfcore/core/cerf_emulator.h>
#include "imx31_avic.h"
#include "imx31_id.h"

namespace cerfcore {

namespace {

/* MCIMX31RM Table 31-2: UART5 at 0x43FB_4000. First UART touched on Zune Keel -
   the ACSSERV.DLL service probes it on the DefaultApp-launch path. */
class Imx31Uart5
    : public cerf_freescale_uart_detail::FreescaleUartBase<0x43FB4000u, 5,
                                                           SocId::Imx31> {
    using FreescaleUartBase::FreescaleUartBase;

protected:
    /* MCIMX31RM Table 2-3 (page 2-8): interrupt 47 "UART5 module", "OR'ed (rx,tx,mint)". */
    void DriveIrq(bool level) override {
        auto& avic = emu_.Get<Imx31Avic>();
        if (level) avic.AssertSource(47u);
        else       avic.DeassertSource(47u);
    }
};

}

REGISTER_SERVICE(Imx31Uart5);

}
