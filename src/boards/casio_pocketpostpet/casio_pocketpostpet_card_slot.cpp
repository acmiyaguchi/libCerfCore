#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include <cerfcore/boards/board_context.h>
#include "casio_pocketpostpet_family_id.h"

namespace {

/* pcmcia.dll CardGetStatus -> sub_135078C reads GIUPIODL D12 (GPIO12); the card is
   present when the pin is LOW (active-low), so an empty socket reads HIGH. */
class CasioPocketpostpetCardSlot : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
    }

    void OnReady() override {
        emu_.Get<Vr41xxGiu>().SetPinLevel(12, true);
    }
};

}  /* namespace */

REGISTER_SERVICE(CasioPocketpostpetCardSlot);
