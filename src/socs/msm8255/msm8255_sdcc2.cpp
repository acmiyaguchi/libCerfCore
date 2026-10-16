#include "msm8255_sdcc_slots.h"
#include "msm8255_sdcc_window_base.h"

namespace {

namespace sdcc = cerf_msm8255_sdcc_detail;

class Msm8255Sdcc2
    : public sdcc::Msm8255SdccWindowBase<
          sdcc::kSdc2Base, sdcc::kSdc2Size, sdcc::kSdc2ResetClock,
          sdcc::kSdc2SlotIndex, sdcc::kSdc2IrqSource0, sdcc::kSdc2IrqSource1,
          sdcc::kSdc2Crci> {
public:
    using Msm8255SdccWindowBase::Msm8255SdccWindowBase;
};

}

REGISTER_SERVICE(Msm8255Sdcc2);
