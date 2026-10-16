#include "msm8255_sdcc_slots.h"
#include "msm8255_sdcc_window_base.h"

namespace {

namespace sdcc = cerf_msm8255_sdcc_detail;

class Msm8255Sdcc4
    : public sdcc::Msm8255SdccWindowBase<
          sdcc::kSdc4Base, sdcc::kSdc4Size, sdcc::kSdc4ResetClock,
          sdcc::kSdc4SlotIndex, sdcc::kSdc4IrqSource0, sdcc::kSdc4IrqSource1,
          sdcc::kSdc4Crci> {
public:
    using Msm8255SdccWindowBase::Msm8255SdccWindowBase;
};

}

REGISTER_SERVICE(Msm8255Sdcc4);
