#include "../../peripherals/wolfson_wm9713/wm9713_master_clocks.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "symbol_hamptons_family_id.h"

namespace {

class SymbolHamptonsCodecClocks : public Wm9713MasterClocks {
public:
    using Wm9713MasterClocks::Wm9713MasterClocks;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SymbolHamptons;
    }

    /* Cirrus Logic WM9713L Rev 4.0 page 21: "AC97 CLK - nominally 24.576MHz, used to generate AC97 BITCLK at
       12.288MHz"; Table 3 (page 22): CLKSRC 1, CLKAX2 0 and CLKMUX 0 take AC97 CLK from MCLKA. */
    uint64_t MclkaHz() const override { return 24576000u; }
    uint64_t MclkbHz() const override { return 0u; }
};

}  // namespace

REGISTER_SERVICE_AS(SymbolHamptonsCodecClocks, Wm9713MasterClocks);
