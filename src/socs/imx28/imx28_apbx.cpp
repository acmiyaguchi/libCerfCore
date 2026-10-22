#include "imx28_apbx.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx28_id.h"

namespace cerfcore {

namespace {

/* Section 7.5.1 CTRL0 reset C000_0000h, RSVD0 [29:0]; section 7.5.118 VERSION 0202_0000h;
   section 7.5.8 CMD RSVD1 [11:8] and RSVD0 [5:4], Figure 7-2 TERMINATEFLUSH [9], HALTONTERMINATE [8];
   sharp_brain_pw_sh4 wavedev2_BU26154.dll c07a999c. */
constexpr uint32_t kCtrl0Reset    = 0xC0000000u;
constexpr uint32_t kCtrl0Writable = 0xC0000000u;
constexpr uint32_t kVersionValue  = 0x02020000u;
constexpr uint32_t kCmdReserved   = 0x00000C30u;

/* Table 7-1 channels; Table 5-1 sources 66 spdif_dma_irq, 68-69 i2c0-1_dma_irq, 70-77
   auart0-3 rx/tx, 78-79 auart4 rx/tx, 80-81 saif0-1_dma_irq. */
constexpr Imx28ApbDmaSpec kSpec = {
    kCtrl0Reset, kCtrl0Writable, 0u, kVersionValue, kCmdReserved, 0u,
    false, false,
    {
        {"AUART4_RX", 78u}, {"AUART4_TX", 79u}, {"SPDIF_TX", 66u},
        {"Empty", kImx28ApbDmaNoSource},
        {"SAIF0", 80u},     {"SAIF1", 81u},
        {"I2C0", 68u},      {"I2C1", 69u},
        {"AUART0_RX", 70u}, {"AUART0_TX", 71u},
        {"AUART1_RX", 72u}, {"AUART1_TX", 73u},
        {"AUART2_RX", 74u}, {"AUART2_TX", 75u},
        {"AUART3_RX", 76u}, {"AUART3_TX", 77u},
    },
};

}  /* namespace */

bool Imx28Apbx::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

const Imx28ApbDmaSpec& Imx28Apbx::Spec() const { return kSpec; }

REGISTER_SERVICE(Imx28Apbx);

}
