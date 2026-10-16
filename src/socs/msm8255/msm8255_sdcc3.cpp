#include "msm8255_sdcc_slots.h"
#include "msm8255_sdcc_window_base.h"

namespace {

namespace sdcc = cerf_msm8255_sdcc_detail;

class Msm8255Sdcc3
    : public sdcc::Msm8255SdccWindowBase<
          sdcc::kSdc3Base, sdcc::kSdc3Size, sdcc::kSdc3ResetClock,
          sdcc::kSdc3SlotIndex, sdcc::kSdc3IrqSource0, sdcc::kSdc3IrqSource1,
          sdcc::kSdc3Crci> {
public:
    using Msm8255SdccWindowBase::Msm8255SdccWindowBase;
};

}

REGISTER_SERVICE(Msm8255Sdcc3);
