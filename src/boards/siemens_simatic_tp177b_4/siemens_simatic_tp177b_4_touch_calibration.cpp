#include "../../socs/s3c2410/s3c2410_touch_calibration.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_tp177b_4_family_id.h"

#include <cstdint>

namespace {

constexpr double kCalXLeftFrac  =  48.0 / 480.0;
constexpr double kCalXRightFrac = 432.0 / 480.0;
constexpr double kCalYTopFrac   =  26.0 / 272.0;
constexpr double kCalYBotFrac   = 246.0 / 272.0;
constexpr double kRawXLeft  = 130.0;    /* avg(126,134) */
constexpr double kRawXRight = 886.5;    /* avg(881,892) */
constexpr double kRawYTop   = 865.0;    /* avg(852,878) */
constexpr double kRawYBot   = 175.0;    /* avg(168,182) */

class SiemensSimaticTp177b4TouchCalibration : public S3C2410TouchCalibration {
public:
    using S3C2410TouchCalibration::S3C2410TouchCalibration;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticTp177b4;
    }

    void MapHostToSample(int host_x, int host_y,
                         double screen_w, double screen_h,
                         uint16_t& sample_x, uint16_t& sample_y) const override {
        const double fx = (double)host_x / screen_w;
        const double fy = (double)host_y / screen_h;
        sample_x = ClampSample(kRawXLeft + (fx - kCalXLeftFrac) *
                               (kRawXRight - kRawXLeft) / (kCalXRightFrac - kCalXLeftFrac));
        sample_y = ClampSample(kRawYTop + (fy - kCalYTopFrac) *
                               (kRawYBot - kRawYTop) / (kCalYBotFrac - kCalYTopFrac));
    }

    /* siemens_tp177b_4inch_v1020 touch.dll sub_3422318 0x03422318 reads ADCDAT0
       as X and ADCDAT1 as Y - not swapped. */
    bool AxisSwap() const override { return false; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimaticTp177b4TouchCalibration, S3C2410TouchCalibration);
