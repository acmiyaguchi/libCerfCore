#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/pcmcia/pcmcia_slot.h"

#include <cstdint>

namespace cerfcore {

/* HP Jornada 820 two-socket PCMCIA controller. The companion ASIC forwards the
   socket status reg (PA 0x181E0800, bit layout per pcmcia.dll sub_12A148C) and
   control reg (PA 0x181E0000, Vcc-enable bits per sub_12A1954) here; this owns
   the slots, the router wiring, and the status-bar insert widgets. */
class HpJornada820Pcmcia : public Service, public PcmciaSlotHost {
public:
    explicit HpJornada820Pcmcia(CerfEmulator& emu);

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    /* Companion-ASIC status forward (PA 0x181E0800, off_12A9458[512]). */
    uint32_t ReadSocketStatus() const;

    /* PcmciaSlotHost. */
    void OnCardDetectChanged(PcmciaSlot& slot) override;
    void OnCardIrqAsserted  (PcmciaSlot& slot) override;
    void OnCardIrqDeasserted(PcmciaSlot& slot) override;

private:
    PcmciaSlot slot0_;
    PcmciaSlot slot1_;
};

}
