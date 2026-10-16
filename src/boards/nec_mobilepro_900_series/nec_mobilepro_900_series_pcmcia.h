#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/pcmcia/pcmcia_slot.h"

#include <cstdint>

namespace cerfcore {

/* NEC MobilePro 900 (P530) two-socket PCMCIA: CF (PXA255 socket 0) + PC Card
   (socket 1), card I/O via PcmciaSpaceRouter, NMC1110 power/reset glue + GPIO
   sense lines. Owns both slots; the L1110 register windows forward here. */
class NecMobilepro900SeriesPcmcia : public Service, public PcmciaSlotHost {
public:
    explicit NecMobilepro900SeriesPcmcia(CerfEmulator& emu);

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    /* Forwarded by the per-socket L1110 register window. socket: 0 = CF,
       1 = PC Card. */
    void ResetSocket(int socket);
    bool SocketHasCard(int socket) const;

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override;
    void OnCardIrqAsserted  (PcmciaSlot& slot) override;
    void OnCardIrqDeasserted(PcmciaSlot& slot) override;

private:
    int SocketOf(const PcmciaSlot& slot) const;

    PcmciaSlot slot0_;   /* CF      (PXA255 socket 0, PA 0x20000000) */
    PcmciaSlot slot1_;   /* PC Card (PXA255 socket 1, PA 0x30000000) */
};

}
