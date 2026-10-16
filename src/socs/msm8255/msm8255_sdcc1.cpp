#include "msm8255_sdcc_slots.h"
#include "msm8255_sdcc_window_base.h"

namespace {

namespace sdcc = cerf_msm8255_sdcc_detail;

class Msm8255Sdcc1
    : public sdcc::Msm8255SdccWindowBase<
          sdcc::kSdc1Base, sdcc::kSdc1Size, sdcc::kSdc1ResetClock,
          sdcc::kSdc1SlotIndex, sdcc::kSdc1IrqSource0, sdcc::kSdc1IrqSource1,
          sdcc::kSdc1Crci> {
public:
    using Msm8255SdccWindowBase::Msm8255SdccWindowBase;
};

}

REGISTER_SERVICE(Msm8255Sdcc1);
