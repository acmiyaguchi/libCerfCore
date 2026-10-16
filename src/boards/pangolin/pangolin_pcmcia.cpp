#include <cerfcore/core/service.h>

#include <cerfcore/boards/board_context.h>
#include "pangolin_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/pcmcia/pcmcia_auto_insert.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

#include <cstdint>

namespace {

constexpr uint32_t kGpioCd  = 6u;
constexpr uint32_t kGpioIrq = 5u;

class PangolinPcmcia : public Service, public PcmciaSlotHost {
public:
    explicit PangolinPcmcia(CerfEmulator& emu)
        : Service(emu), slot_(emu, *this, "PC Card slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Pangolin;
    }

    void OnReady() override {
        emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot_, nullptr);
        emu_.Get<HostWidgetRegistry>().Register(&slot_);

        auto& gpio = emu_.Get<Sa11xxGpio>();
        gpio.DriveInputPin(kGpioCd, true);    /* empty: CF_CD idles high (no card) */
        gpio.DriveInputPin(kGpioIrq, true);   /* READY/nIREQ deasserted (idle high) */

        emu_.Get<PcmciaAutoInsert>().InsertDefaultNetworkCard(slot_);
    }

    void OnShutdown() override { slot_.OnShutdown(); }

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override {
        const bool present = slot.HasCard();
        slot.SetPowered(present);
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioCd, !present);   /* present => CD low */
    }
    void OnCardIrqAsserted(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, false);     /* nIREQ low = asserted */
    }
    void OnCardIrqDeasserted(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, true);
    }

private:
    PcmciaSlot slot_;
};

}  /* namespace */

REGISTER_SERVICE(PangolinPcmcia);
