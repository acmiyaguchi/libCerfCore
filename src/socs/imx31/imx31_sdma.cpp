#include "imx31_sdma.h"

#include "imx31_avic.h"
#include <cerfcore/core/cerf_emulator.h>

#include <cstdint>

namespace {

/* SDMA AP interrupt = AVIC source 34 (MCIMX31RM Table 2-3). */
constexpr uint32_t kAvicSourceSdma = 34u;
/* i.MX31 divergent register (MCIMX31RM Table 40-10): EVT_MIRROR @0x54,
   a read-only DMA-request mirror. */
constexpr uint32_t kOffEvtMirror = 0x54u;

}  /* namespace */

void Imx31Sdma::AssertIrqLine() {
    emu_.Get<Imx31Avic>().AssertSource(kAvicSourceSdma);
}

void Imx31Sdma::DeassertIrqLine() {
    emu_.Get<Imx31Avic>().DeassertSource(kAvicSourceSdma);
}

bool Imx31Sdma::ReadExtra(uint32_t off, uint32_t& out) {
    if (off == kOffEvtMirror) { out = 0; return true; }   /* no DMA-request events */
    return false;
}

REGISTER_SERVICE(Imx31Sdma);
