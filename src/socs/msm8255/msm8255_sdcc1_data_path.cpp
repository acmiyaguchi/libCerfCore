#include "msm8255_sdcc_data_path.h"
#include "msm8255_sdcc_slots.h"

namespace {

namespace sdcc = cerf_msm8255_sdcc_detail;

using Msm8255Sdcc1DataPathBase =
    sdcc::Msm8255SdccDataPath<sdcc::kSdc1Base, sdcc::kSdc1ResetClock,
                              sdcc::kSdc1Crci>;

class Msm8255Sdcc1DataPath : public Msm8255Sdcc1DataPathBase {
public:
    using Msm8255Sdcc1DataPathBase::Msm8255Sdcc1DataPathBase;
};

}

REGISTER_SERVICE_AS(Msm8255Sdcc1DataPath, Msm8255Sdcc1DataPathBase);
