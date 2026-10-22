#include "imx28_apbh.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx28_id.h"

namespace cerfcore {

namespace {

/* Section 6.5.1 CTRL0 reset E000_0000h, RSVD0 [27:16]; section 6.5.120 VERSION 0301_0000h;
   Table 6-3 reserved bits [11:9]; Table 6-1 channels 4-11 are the NAND (GPMI) channels. */
constexpr uint32_t kCtrl0Reset     = 0xE0000000u;
constexpr uint32_t kCtrl0Writable  = 0xF000FFFFu;
constexpr uint32_t kClkgateChannel = 0x0000FFFFu;
constexpr uint32_t kVersionValue   = 0x03010000u;
constexpr uint32_t kCmdReserved    = 0x00000E00u;
constexpr uint32_t kNandChannels   = 0x00000FF0u;

/* Table 5-1 sources 82-85 ssp0-3_dma_irq, 86 lcdif_dma_irq, 87 hsadc_dma_irq, 88 gpmi_dma_irq. */
constexpr Imx28ApbDmaSpec kSpec = {
    kCtrl0Reset, kCtrl0Writable, kClkgateChannel, kVersionValue, kCmdReserved, kNandChannels,
    true, true,
    {
        {"SSP0", 82u},  {"SSP1", 83u},  {"SSP2", 84u},  {"SSP3", 85u},
        {"GPMI0", 88u}, {"GPMI1", 88u}, {"GPMI2", 88u}, {"GPMI3", 88u},
        {"GPMI4", 88u}, {"GPMI5", 88u}, {"GPMI6", 88u}, {"GPMI7", 88u},
        {"HSADC", 87u}, {"LCDIF", 86u},
        {"Empty", kImx28ApbDmaNoSource}, {"Empty", kImx28ApbDmaNoSource},
    },
};

}  /* namespace */

bool Imx28Apbh::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

const Imx28ApbDmaSpec& Imx28Apbh::Spec() const { return kSpec; }

REGISTER_SERVICE(Imx28Apbh);

}
