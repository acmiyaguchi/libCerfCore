#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/service.h>
#include "../../peripherals/cirrus_pd6710/pd6710_controller.h"
#include "../../peripherals/pcmcia/pcmcia_auto_insert.h"

namespace cerfcore {

namespace {

class SamsungSmdk2410DefaultPcmciaCard : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    void OnReady() override {
        emu_.Get<PcmciaAutoInsert>().InsertDefaultNetworkCard(
            emu_.Get<Pd6710Controller>().Slot());
    }
};

}  /* namespace */

REGISTER_SERVICE(SamsungSmdk2410DefaultPcmciaCard);

}
