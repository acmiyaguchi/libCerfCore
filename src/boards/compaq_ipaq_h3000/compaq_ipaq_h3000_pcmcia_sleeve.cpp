#include "compaq_ipaq_h3000_egpio_sink.h"

#include <cerfcore/boards/board_context.h>
#include "compaq_ipaq_h3000_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

namespace cerfcore {

namespace {

/* iPAQ H36xx dual-PCMCIA sleeve, pin map per the ROM PCMCIA PDD's
   GPLR tests: CD = GPIO17/10 (low = present), READY_nIREQ = GPIO21/11,
   sleeve presence = GPIO22 + GPIO27 both low, power = EGPIO bit 5
   (option-pack enable, set by the PDD on socket power-up). */
constexpr uint32_t kGpioCd[2]    = { 17u, 10u };
constexpr uint32_t kGpioReady[2] = { 21u, 11u };
constexpr uint32_t kGpioSleeveDetectA = 22u;
constexpr uint32_t kGpioSleeveDetectB = 27u;
constexpr uint32_t kEgpioOptPowerOn   = 1u << 5;

class CompaqIpaqH3000PcmciaSleeve : public CompaqIpaqH3000EgpioSink,
                             public PcmciaSlotHost {
public:
    explicit CompaqIpaqH3000PcmciaSleeve(CerfEmulator& emu)
        : CompaqIpaqH3000EgpioSink(emu),
          slot0_(emu, *this, "PC Card slot 1"),
          slot1_(emu, *this, "PC Card slot 2"),
          slots_{ &slot0_, &slot1_ } {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
    }

    void OnReady() override {
        emu_.Get<PcmciaSpaceRouter>().ProvideSockets(slots_[0], slots_[1]);
        auto& widgets = emu_.Get<HostWidgetRegistry>();
        widgets.Register(slots_[0]);
        widgets.Register(slots_[1]);

        auto& gpio = emu_.Get<Sa11xxGpio>();
        gpio.DriveInputPin(kGpioSleeveDetectA, false);
        gpio.DriveInputPin(kGpioSleeveDetectB, false);
        for (int n = 0; n < 2; ++n) {
            gpio.DriveInputPin(kGpioCd[n], true);      /* empty */
            gpio.DriveInputPin(kGpioReady[n], true);
        }
    }

    void OnShutdown() override {
        slot0_.OnShutdown();
        slot1_.OnShutdown();
    }

    void OnEgpioChanged(uint32_t latched) override {
        const bool on = (latched & kEgpioOptPowerOn) != 0u;
        slots_[0]->SetPowered(on);
        slots_[1]->SetPowered(on);
    }

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override {
        const int n = SocketOf(slot);
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioCd[n], !slot.HasCard());
    }
    void OnCardIrqAsserted(PcmciaSlot& slot) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioReady[SocketOf(slot)],
                                             false);
    }
    void OnCardIrqDeasserted(PcmciaSlot& slot) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioReady[SocketOf(slot)],
                                             true);
    }

private:
    int SocketOf(const PcmciaSlot& slot) const {
        return &slot == slots_[0] ? 0 : 1;
    }

    PcmciaSlot  slot0_;
    PcmciaSlot  slot1_;
    PcmciaSlot* slots_[2];
};

}  /* namespace */

REGISTER_SERVICE_AS(CompaqIpaqH3000PcmciaSleeve, CompaqIpaqH3000EgpioSink);

}
