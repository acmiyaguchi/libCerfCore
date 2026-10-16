#include "pxa2xx_ac97_modem_out.h"

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

/* Intel PXA255 Developer's Manual Table 5-5 (page 5-13) and Intel PXA27x Developer's Manual Table 5-10
   (page 5-24): AC97 modem transmit FIFO 0x4050_0140 on DRCMR 0x4000_0128. */
constexpr uint32_t kModr      = 0x40500140u;
constexpr uint32_t kRequestTx = 10u;

}  // namespace

uint32_t Pxa2xxAc97ModemOut::FifoPa() const { return kModr; }

uint32_t Pxa2xxAc97ModemOut::Request() const { return kRequestTx; }

/* AC '97 Component Specification Revision 2.2 section 4.2.1.2 (page 28): "SLOTREQ bits for fixed rate,
   powered down, and all unsupported Slots should be driven with 0s". */
Pxa2xxSlotPace Pxa2xxAc97ModemOut::SlotPace() { return Pxa2xxSlotPace{Ac97Codec::kFrameRateHz, 1u}; }

REGISTER_SERVICE(Pxa2xxAc97ModemOut);

}
