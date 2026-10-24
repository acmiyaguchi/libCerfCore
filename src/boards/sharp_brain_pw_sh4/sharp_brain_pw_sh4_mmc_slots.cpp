#include "../../peripherals/mmc/mmc_card.h"
#include "../../peripherals/mmc/mmc_slot_provider.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "sharp_brain_pw_sh4_family_id.h"
#include "sharp_brain_pw_sh4_sd.h"

namespace cerfcore {

class SharpBrainPwSh4MmcSlots : public MmcSlotProvider {
public:
    using MmcSlotProvider::MmcSlotProvider;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    /* sharp_brain_pw_sh4 sdhc.dll SHC_Init c0782c1c: slot 0 drives SSP0 (eMMC), slot 1 drives SSP1 (SD). */
    MmcCard* CardForSlot(uint32_t slot) override {
        if (slot == 0u) {
            auto* card = emu_.TryGet<MmcCard>();
            return (card != nullptr && card->SlotIndex() == 0u) ? card : nullptr;
        }
        if (slot == 1u) {
            auto* sd = emu_.TryGet<SharpBrainPwSh4Sd>();
            return (sd != nullptr && sd->IsCardPresent()) ? sd : nullptr;
        }
        return nullptr;
    }
};

REGISTER_SERVICE_AS(SharpBrainPwSh4MmcSlots, MmcSlotProvider);

}
