#include "imx28_lradc_adc.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "imx28_lradc_inputs.h"

namespace cerfcore {

namespace {

/* Section 38.5.6: "12-bit result"; section 38.2: "maximum analog input voltage into the LRADC is
   1.85 V". */
constexpr uint32_t kFullScaleMv = 1850u;
constexpr uint32_t kCodeMax     = 0xFFFu;
constexpr uint32_t kCodeCount   = 0x1000u;

/* Table 38-1 channels 0-6 pins, 7 BATT; section 38.2 BATT built-in 4:1 divider. */
constexpr uint32_t kLastPin     = 6u;
constexpr uint32_t kBattChannel = 7u;
constexpr uint32_t kBattDivider = 4u;

/* Section 38.5.3 CTRL2 DIVIDE_BY_TWO [31:24]; section 38.5.20 CONVERSION SCALE_FACTOR [17:16],
   0x2 LI_ION. */
constexpr uint32_t kDivideByTwoShift = 24u;
constexpr uint32_t kScaleShift       = 16u;
constexpr uint32_t kScaleMask        = 0x3u;
constexpr uint32_t kScaleLiIon       = 0x2u;

}  /* namespace */

uint32_t Imx28LradcAdc::Convert(uint32_t channel, uint32_t select, uint32_t ctrl0, uint32_t ctrl2,
                                uint32_t conversion) {
    Fatal& fatal = emu_.Get<Fatal>();
    Imx28LradcInputs* inputs = emu_.TryGet<Imx28LradcInputs>();
    if (inputs == nullptr) {
        fatal.Die("Imx28 %08X: channel %u select %u conversion with no input model", kImx28LradcBase,
                  channel, select);
    }
    const bool divide = ((ctrl2 >> (kDivideByTwoShift + channel)) & 1u) != 0u;
    uint32_t mv      = 0u;
    uint32_t divisor = 1u;
    if (select <= kLastPin) {
        mv = inputs->PinMillivolts(select, ctrl0, ctrl2);
        if (divide) divisor = 2u;
    } else if (select == kBattChannel) {
        if (divide || ((conversion >> kScaleShift) & kScaleMask) != kScaleLiIon) {
            fatal.Die("Imx28 %08X: channel %u BATT conversion with CTRL2 %08X CONVERSION %08X",
                      kImx28LradcBase, channel, ctrl2, conversion);
        }
        mv      = inputs->BatteryMillivolts();
        divisor = kBattDivider;
    } else {
        fatal.Die("Imx28 %08X: channel %u select %u conversion with no source model", kImx28LradcBase,
                  channel, select);
    }
    const uint32_t code = static_cast<uint32_t>(static_cast<uint64_t>(mv) * kCodeCount /
                                                (static_cast<uint64_t>(kFullScaleMv) * divisor));
    return code > kCodeMax ? kCodeMax : code;
}

}
