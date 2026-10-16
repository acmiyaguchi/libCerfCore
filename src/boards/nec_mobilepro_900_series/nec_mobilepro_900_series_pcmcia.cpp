#include "nec_mobilepro_900_series_pcmcia.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/pcmcia/pcmcia_auto_insert.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/pxa255/pxa255_gpio.h"
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"

namespace cerfcore {

namespace {

/* Per-socket GPIO (Linux jlime_v2.6.34-hpc_mp900.c mp900_pin_config[]). */
constexpr uint32_t kGpioNcd    [2] = { 11u, 13u };  /* nCD,  socket 0 CF / 1 PC */
constexpr uint32_t kGpioPrdy   [2] = {  5u,  7u };  /* PRDY (ready), idles high  */
constexpr uint32_t kGpioCardIrq[2] = { 75u, 76u };  /* card IREQ, active-low      */

}  /* namespace */

NecMobilepro900SeriesPcmcia::NecMobilepro900SeriesPcmcia(CerfEmulator& emu)
    : Service(emu),
      slot0_(emu, *this, "CompactFlash slot"),
      slot1_(emu, *this, "PC Card slot") {}

bool NecMobilepro900SeriesPcmcia::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
}

void NecMobilepro900SeriesPcmcia::OnReady() {
    emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot0_, &slot1_);
    auto& widgets = emu_.Get<HostWidgetRegistry>();
    widgets.Register(&slot0_);
    widgets.Register(&slot1_);

    auto& gpio = emu_.Get<Pxa255Gpio>();
    for (int s = 0; s < 2; ++s) {
        gpio.SetInputLevel(kGpioNcd[s],     true);  /* empty: nCD idles high */
        gpio.SetInputLevel(kGpioPrdy[s],    true);  /* ready line idles high */
        gpio.SetInputLevel(kGpioCardIrq[s], true);  /* IREQ idles high (active-low) */
    }

    emu_.Get<PcmciaAutoInsert>().InsertDefaultNetworkCard(slot1_);
}

void NecMobilepro900SeriesPcmcia::OnShutdown() {
    slot0_.OnShutdown();
    slot1_.OnShutdown();
}

int NecMobilepro900SeriesPcmcia::SocketOf(const PcmciaSlot& slot) const {
    return (&slot == &slot1_) ? 1 : 0;
}

void NecMobilepro900SeriesPcmcia::ResetSocket(int socket) {
    (socket == 1 ? slot1_ : slot0_).ResetCard();
}

bool NecMobilepro900SeriesPcmcia::SocketHasCard(int socket) const {
    return (socket == 1 ? slot1_ : slot0_).HasCard();
}

void NecMobilepro900SeriesPcmcia::OnCardDetectChanged(PcmciaSlot& slot) {
    const int  socket  = SocketOf(slot);
    const bool present = slot.HasCard();
    slot.SetPowered(present);
    emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioNcd[socket], !present);
}

void NecMobilepro900SeriesPcmcia::OnCardIrqAsserted(PcmciaSlot& slot) {
    emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioCardIrq[SocketOf(slot)], false);
}

void NecMobilepro900SeriesPcmcia::OnCardIrqDeasserted(PcmciaSlot& slot) {
    emu_.Get<Pxa255Gpio>().SetInputLevel(kGpioCardIrq[SocketOf(slot)], true);
}

REGISTER_SERVICE(NecMobilepro900SeriesPcmcia);

}
