#include "../freescale_ssi_sdma_wiring.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "imx31_id.h"
#include "imx31_sdma.h"
#include "imx31_ssi1.h"
#include "imx31_ssi2.h"

namespace cerfcore {

namespace {

class Imx31SsiSdmaWiring : public FreescaleSsiSdmaWiring {
public:
    using FreescaleSsiSdmaWiring::FreescaleSsiSdmaWiring;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }

    FreescaleSdmaChannelHost& Sdma() override { return emu_.Get<Imx31Sdma>(); }

    FreescaleSsiCore& Core(uint32_t ssi) override {
        switch (ssi) {
            case 1u: return emu_.Get<Imx31Ssi1>().Core();
            case 2u: return emu_.Get<Imx31Ssi2>().Core();
        }
        emu_.Get<Fatal>().Die("Imx31SsiSdmaWiring: SSI%u is not an i.MX31 SSI", ssi);
    }

    uint32_t SsiCount() const override { return 2u; }

    /* MCIMX31RM Table 2-5 (page 2-10): SSI1 transmit 1 / 2 = 29 / 27, SSI2 25 / 23. */
    bool TxEventFifo(int event, uint32_t& ssi, uint32_t& fifo) const override {
        switch (event) {
            case 29: ssi = 1u; fifo = 0u; return true;
            case 27: ssi = 1u; fifo = 1u; return true;
            case 25: ssi = 2u; fifo = 0u; return true;
            case 23: ssi = 2u; fifo = 1u; return true;
        }
        return false;
    }

    /* MCIMX31RM Table 2-5 (page 2-10): SSI1 receive 1 / 2 = 28 / 26, SSI2 24 / 22. */
    bool RxEventFifo(int event, uint32_t& ssi, uint32_t& fifo) const override {
        switch (event) {
            case 28: ssi = 1u; fifo = 0u; return true;
            case 26: ssi = 1u; fifo = 1u; return true;
            case 24: ssi = 2u; fifo = 0u; return true;
            case 22: ssi = 2u; fifo = 1u; return true;
        }
        return false;
    }

    uint32_t RxSampleBytes(uint32_t) override { return 2u; }
};

}

REGISTER_SERVICE_AS(Imx31SsiSdmaWiring, FreescaleSsiSdmaWiring);

}
