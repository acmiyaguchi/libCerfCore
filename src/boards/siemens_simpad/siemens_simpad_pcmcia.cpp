#include "siemens_simpad_cs3_sink.h"

#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/pcmcia/pcmcia_auto_insert.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

namespace cerfcore {

namespace {

/* SIMpad SL4 single PC Card socket (SA-1110 socket 0, window PA 0x20000000).
   CF_CD = GPIO24 active-low (0 = present), CF_IRQ/READY = GPIO1. */
constexpr uint32_t kGpioCd  = 24u;   /* GPIO_CF_CD  */
constexpr uint32_t kGpioIrq = 1u;    /* GPIO_CF_IRQ */

class SiemensSimpadPcmcia : public SiemensSimpadCs3Sink, public PcmciaSlotHost {
public:
    explicit SiemensSimpadPcmcia(CerfEmulator& emu)
        : SiemensSimpadCs3Sink(emu), slot_(emu, *this, "PC Card slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    void OnReady() override {
        emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot_, nullptr);
        emu_.Get<HostWidgetRegistry>().Register(&slot_);

        auto& gpio = emu_.Get<Sa11xxGpio>();
        gpio.DriveInputPin(kGpioCd, true);    /* empty: CF_CD idles high     */
        gpio.DriveInputPin(kGpioIrq, true);   /* READY/nIREQ deasserted high */

        emu_.Get<PcmciaAutoInsert>().InsertDefaultNetworkCard(slot_);
    }

    void OnShutdown() override { slot_.OnShutdown(); }

    /* The card keeps its CIS/COR across the SA-1110 sleep: the PDD's CS3 Vcc/RESET
       writes are power-management, not a config-destroying power-cycle, so the card
       is powered-while-occupied (OnCardDetectChanged) and these bits are inert here. */
    void OnCs3LatchChanged(uint16_t /*latch*/) override {}

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override {
        const bool present = slot.HasCard();
        slot.SetPowered(present);
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioCd, !present);
    }
    void OnCardIrqAsserted(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, false);
    }
    void OnCardIrqDeasserted(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, true);
    }

private:
    PcmciaSlot slot_;
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimpadPcmcia, SiemensSimpadCs3Sink);

}
