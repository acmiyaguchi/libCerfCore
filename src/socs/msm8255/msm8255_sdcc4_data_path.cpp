#include "msm8255_sdcc_data_path.h"
#include "msm8255_sdcc_slots.h"

namespace {

namespace sdcc = cerf_msm8255_sdcc_detail;

using Msm8255Sdcc4DataPathBase =
    sdcc::Msm8255SdccDataPath<sdcc::kSdc4Base, sdcc::kSdc4ResetClock,
                              sdcc::kSdc4Crci>;

class Msm8255Sdcc4DataPath : public Msm8255Sdcc4DataPathBase {
public:
    using Msm8255Sdcc4DataPathBase::Msm8255Sdcc4DataPathBase;
};

}

REGISTER_SERVICE_AS(Msm8255Sdcc4DataPath, Msm8255Sdcc4DataPathBase);
