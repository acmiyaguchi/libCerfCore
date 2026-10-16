#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include <cerfcore/boards/board_context.h>
#include "casio_tomcat_family_id.h"

namespace {

/* casio_cassiopeia_e55 nk.exe cold boot requires GPIO19=H (sub_9E814D18 @0x9E814D20,
   sub_9E815960, sub_9E815F9C @0x9E816160), GPIO21=H (sub_9E814D4C @0x9E814D50,
   sub_9E814EA8 @0x9E814EAC), GPIO17=L (sub_9E814D4C @0x9E814DB8); each failed poll ends
   in cop0 0x23 @0x9E816364. GIUPIODH D3/D5/D1 (VR4111 UM U13137EJ2V0UM 19.2.4 p.402). */
class CasioTomcatBootGpio : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioTomcat;
    }

    void OnReady() override {
        auto& giu = emu_.Get<Vr41xxGiu>();
        giu.SetPinLevel(19, true);
        giu.SetPinLevel(21, true);
        giu.SetPinLevel(17, false);
    }
};

}  /* namespace */

REGISTER_SERVICE(CasioTomcatBootGpio);
