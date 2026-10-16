#include "../../socs/imx51/imx51_clock_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"

namespace {

/* IMX51CEC Table 3 XTAL/EXTAL: "Freescale BSP (Board Support Package) software
   requires 24 MHz on EXTAL." */
class FordSyncGen2ClockInput : public Imx51ClockInput {
public:
    using Imx51ClockInput::Imx51ClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }

    uint64_t OscHz() const override { return 24000000u; }

    /* MC13892 datasheet: CLK32KMCU is "intended as the CKIL input to the system
       processor", Table 22 "32.768 kHz". */
    uint64_t CkilHz() const override { return 32768u; }

    /* sync_2_sec nk.exe sub_8010D8C8 (IDA 40353): CKIH 24576000 Hz into the DDK frequency
       table; its ssi_lp_apm_clk takes 0 Hz when CSCMR1 ssi_apm_clk_sel selects CAMP2 of CKIH2. */
    uint64_t CkihHz() const override { return 24576000u; }
    uint64_t Ckih2Hz() const override { return 0u; }
};

}

REGISTER_SERVICE_AS(FordSyncGen2ClockInput, Imx51ClockInput);
