#include "msm8255_sdcc_data_path.h"
#include "msm8255_sdcc_slots.h"

namespace {

namespace sdcc = cerf_msm8255_sdcc_detail;

using Msm8255Sdcc3DataPathBase =
    sdcc::Msm8255SdccDataPath<sdcc::kSdc3Base, sdcc::kSdc3ResetClock,
                              sdcc::kSdc3Crci>;

class Msm8255Sdcc3DataPath : public Msm8255Sdcc3DataPathBase {
public:
    using Msm8255Sdcc3DataPathBase::Msm8255Sdcc3DataPathBase;
};

}

REGISTER_SERVICE_AS(Msm8255Sdcc3DataPath, Msm8255Sdcc3DataPathBase);
