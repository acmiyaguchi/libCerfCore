#include "../freescale_ssi_sdma_wiring.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../freescale_ssi_external_clock.h"
#include "imx51_id.h"
#include "imx51_sdma.h"
#include "imx51_ssi1.h"
#include "imx51_ssi2.h"
#include "imx51_ssi3.h"

namespace cerfcore {

namespace {

class Imx51SsiSdmaWiring : public FreescaleSsiSdmaWiring {
public:
    using FreescaleSsiSdmaWiring::FreescaleSsiSdmaWiring;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx51;
    }

    FreescaleSdmaChannelHost& Sdma() override { return emu_.Get<Imx51Sdma>(); }

    FreescaleSsiCore& Core(uint32_t ssi) override {
        switch (ssi) {
            case 1u: return emu_.Get<Imx51Ssi1>().Core();
            case 2u: return emu_.Get<Imx51Ssi2>().Core();
            case 3u: return emu_.Get<Imx51Ssi3>().Core();
        }
        emu_.Get<Fatal>().Die("Imx51SsiSdmaWiring: SSI%u is not an i.MX51 SSI", ssi);
    }

    uint32_t SsiCount() const override { return 3u; }

    /* MCIMX51RM Table 3-3 (pages 3-5 / 3-6): SSI1 transmit 1 / 2 = 29 / 27, SSI2 25 / 23, SSI3
       47 / 37. */
    bool TxEventFifo(int event, uint32_t& ssi, uint32_t& fifo) const override {
        switch (event) {
            case 29: ssi = 1u; fifo = 0u; return true;
            case 27: ssi = 1u; fifo = 1u; return true;
            case 25: ssi = 2u; fifo = 0u; return true;
            case 23: ssi = 2u; fifo = 1u; return true;
            case 47: ssi = 3u; fifo = 0u; return true;
            case 37: ssi = 3u; fifo = 1u; return true;
        }
        return false;
    }

    /* MCIMX51RM Table 3-3 (pages 3-5 / 3-6): SSI1 receive 1 / 2 = 28 / 26, SSI2 24 / 22, SSI3
       46 / 35. */
    bool RxEventFifo(int event, uint32_t& ssi, uint32_t& fifo) const override {
        switch (event) {
            case 28: ssi = 1u; fifo = 0u; return true;
            case 26: ssi = 1u; fifo = 1u; return true;
            case 24: ssi = 2u; fifo = 0u; return true;
            case 22: ssi = 2u; fifo = 1u; return true;
            case 46: ssi = 3u; fifo = 0u; return true;
            case 35: ssi = 3u; fifo = 1u; return true;
        }
        return false;
    }

    uint32_t RxSampleBytes(uint32_t ssi) override {
        return emu_.Get<FreescaleSsiExternalClock>().Format(ssi).bits / 8u;
    }
};

}

REGISTER_SERVICE_AS(Imx51SsiSdmaWiring, FreescaleSsiSdmaWiring);

}
