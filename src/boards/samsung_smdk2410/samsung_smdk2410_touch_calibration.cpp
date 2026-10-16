#include "../../socs/s3c2410/s3c2410_touch_calibration.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

#include <cstdint>

namespace {

/* devemu_wm5 touch.dll sub_153170C 0x153170C. */
constexpr double kSampleXAtLeftEdge = 85.0;
constexpr double kSampleXSpan       = 880.0;
constexpr double kSampleYSpan       = 875.0;

/* devemu_wm5 touch.dll sub_15317AC 0x15317AC takes X from ADCDAT1 and Y as
   1023 - ADCDAT0. */
constexpr double kSampleYAtTopEdge  = 1023.0 - 105.0;

class SamsungSmdk2410TouchCalibration : public S3C2410TouchCalibration {
public:
    using S3C2410TouchCalibration::S3C2410TouchCalibration;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    void MapHostToSample(int host_x, int host_y,
                         double screen_w, double screen_h,
                         uint16_t& sample_x, uint16_t& sample_y) const override {
        sample_x = ClampSample(kSampleXAtLeftEdge +
                               (host_x + 0.5) * kSampleXSpan / screen_w);
        sample_y = ClampSample(kSampleYAtTopEdge -
                               (host_y + 0.5) * kSampleYSpan / screen_h);
    }

    bool AxisSwap() const override { return true; }
};

}

REGISTER_SERVICE_AS(SamsungSmdk2410TouchCalibration, S3C2410TouchCalibration);
