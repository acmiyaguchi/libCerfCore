#include "hp_jornada_820_pcmcia.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"
#include "hp_jornada_820_companion_asic.h"

namespace cerfcore {

namespace {

/* Socket status word (PA 0x181E0800), decoded from pcmcia.dll sub_12A148C: per
   socket nCD is active-low (bit clear = card present), RDY high = ready,
   WP high = write-protected, BVD1/BVD2 high = battery good. */
constexpr uint32_t kS0Ncd = 0x4u,    kS0Rdy = 0x1u,
                   kS0Bvd1 = 0x400u, kS0Bvd2 = 0x800u;
constexpr uint32_t kS1Ncd = 0x8u,    kS1Rdy = 0x2u,
                   kS1Bvd1 = 0x1000u, kS1Bvd2 = 0x2000u;

}  /* namespace */

HpJornada820Pcmcia::HpJornada820Pcmcia(CerfEmulator& emu)
    : Service(emu),
      slot0_(emu, *this, "PC Card slot"),
      slot1_(emu, *this, "CompactFlash slot") {}

bool HpJornada820Pcmcia::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada820;
}

void HpJornada820Pcmcia::OnReady() {
    emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot0_, &slot1_);
    auto& widgets = emu_.Get<HostWidgetRegistry>();
    widgets.Register(&slot0_);
    widgets.Register(&slot1_);
}

void HpJornada820Pcmcia::OnShutdown() {
    slot0_.OnShutdown();
    slot1_.OnShutdown();
}

uint32_t HpJornada820Pcmcia::ReadSocketStatus() const {
    uint32_t s = kS0Bvd1 | kS0Bvd2 | kS1Bvd1 | kS1Bvd2;   /* batteries good */
    s |= slot0_.HasCard() ? kS0Rdy : kS0Ncd;
    s |= slot1_.HasCard() ? kS1Rdy : kS1Ncd;
    return s;
}

void HpJornada820Pcmcia::OnCardDetectChanged(PcmciaSlot& slot) {
    /* Power on presence, NOT the control-reg Vcc bit: ne2000.dll DetectNE2000
       writes config regs + probes card I/O before the PDD's SetSocket Vcc, so
       gating power on that bit leaves the card unpowered during detection and
       it reads back 0xFF -> "Unidentified PCCard". */
    slot.SetPowered(slot.HasCard());
    /* A runtime insert/eject must raise the companion status-change interrupt
       (SYSINTR 27) or the PDD detect poller (pcmcia.dll sub_12A59E8), which
       waits INFINITE on that interrupt, never re-scans the socket. */
    const int socket = (&slot == &slot1_) ? 1 : 0;
    emu_.Get<HpJornada820CompanionAsic>().RaisePcmciaStatusChange(socket);
}

/* Card IREQ is aggregated by the companion ASIC: it latches the per-socket
   pending bit (companion 0x166400 bit25/26, read by the OAL OEMInterruptHandler
   sub_80059BB0 -> SYSINTR 28) and drives the shared GPIO14 line. The OAL clears
   pending W1C when it acks, so deassert is a no-op here. */
void HpJornada820Pcmcia::OnCardIrqAsserted(PcmciaSlot& slot) {
    const int socket = (&slot == &slot1_) ? 1 : 0;
    emu_.Get<HpJornada820CompanionAsic>().RaisePcmciaCardIrq(socket);
}
void HpJornada820Pcmcia::OnCardIrqDeasserted(PcmciaSlot&) {}

REGISTER_SERVICE(HpJornada820Pcmcia);

}
