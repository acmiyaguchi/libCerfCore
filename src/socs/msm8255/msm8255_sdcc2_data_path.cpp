#include "msm8255_sdcc_data_path.h"
#include "msm8255_sdcc_slots.h"

namespace {

namespace sdcc = cerf_msm8255_sdcc_detail;

using Msm8255Sdcc2DataPathBase =
    sdcc::Msm8255SdccDataPath<sdcc::kSdc2Base, sdcc::kSdc2ResetClock,
                              sdcc::kSdc2Crci>;

class Msm8255Sdcc2DataPath : public Msm8255Sdcc2DataPathBase {
public:
    using Msm8255Sdcc2DataPathBase::Msm8255Sdcc2DataPathBase;
};

}

REGISTER_SERVICE_AS(Msm8255Sdcc2DataPath, Msm8255Sdcc2DataPathBase);
