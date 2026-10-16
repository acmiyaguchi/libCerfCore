#include <cerfcore/host/host_presentation.h>
#include <cerfcore/host/touch_input.h>
#include "../../peripherals/ti_tsc2046/ti_tsc2046_touch.h"
#include "../../socs/omap3530/omap3530_gpio_bus.h"
#include <cerfcore/boards/board_context.h>
#include "ti_omap35x_evm_family_id.h"

#include <cerfcore/core/cerf_emulator.h>

#include <algorithm>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kPenIrqGpio  = 175;
constexpr uint32_t kAdcRangeMax = 4095;

class TiOmap35xEvmTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
    }

    void OnPenDown    (int x, int y) override { Apply(x, y, true);  }
    void OnPenMove    (int x, int y) override { Apply(x, y, true);  }
    void OnPenUp      (int x, int y) override { Apply(x, y, false); }
    void OnCaptureLost()             override { ApplyUp(); }

private:
    static uint16_t ToAdc(int pixel, int extent,
                          int adc_at_screen_lo, int adc_at_screen_hi) {
        if (extent <= 0) return 0;
        const long screen_lo = static_cast<long>(extent) / 5;
        const long screen_hi = static_cast<long>(extent) - screen_lo;
        const long span      = screen_hi - screen_lo;
        if (span <= 0) return 0;
        const long adc = static_cast<long>(adc_at_screen_lo) +
                         (static_cast<long>(pixel) - screen_lo) *
                         (static_cast<long>(adc_at_screen_hi) -
                          static_cast<long>(adc_at_screen_lo)) / span;
        return static_cast<uint16_t>(
            std::clamp<long>(adc, 0, static_cast<long>(kAdcRangeMax)));
    }

    void Apply(int host_x, int host_y, bool pen_down) {
        auto& hc  = emu_.Get<HostPresentation>();
        const uint32_t client_w = hc.SurfaceWidth();
        const uint32_t client_h = hc.SurfaceHeight();
        constexpr int kAdcXLo = 3095;  /* avg(3095, 3096) at screen W/5 */
        constexpr int kAdcXHi = 938;   /* avg(932,  944)  at screen W-W/5 */
        constexpr int kAdcYLo = 3125;  /* avg(3141, 3110) at screen H/5 */
        constexpr int kAdcYHi = 896;   /* avg(908,  885)  at screen H-H/5 */
        const uint16_t adc_x = ToAdc(host_x, client_w, kAdcXLo, kAdcXHi);
        const uint16_t adc_y = ToAdc(host_y, client_h, kAdcYLo, kAdcYHi);
        emu_.Get<Tsc2046Touch>().SetState(adc_x, adc_y, pen_down);
        /* PENIRQ is active-low: drive pin low while pen is down. */
        emu_.Get<Omap3530GpioBus>().SetInputPin(kPenIrqGpio, !pen_down);
    }

    void ApplyUp() {
        emu_.Get<Tsc2046Touch>().SetState(0, 0, false);
        emu_.Get<Omap3530GpioBus>().SetInputPin(kPenIrqGpio, true);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(TiOmap35xEvmTouchInput, TouchInput);

}
