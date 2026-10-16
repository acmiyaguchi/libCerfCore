#include "../../peripherals/touch/guest_clock_touch_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"
#include "nec_mobilepro_900_series_pco_companion.h"

namespace cerfcore {

namespace {

/* nec_mobilepro_900_hpc2000 XIP.BIN default.reg HARDWARE\DEVICEMAP\TOUCH CalibrationData
   "504,435 135,687 131,175 856,179 852,690" at touch.dll sub_1141878's five 640x240 targets. */
constexpr GuestClockTouchInput::PanelRange kPanel{45u, 945u, 753u, 116u};

class NecMobilepro900SeriesTouchInput : public GuestClockTouchInput {
public:
    using GuestClockTouchInput::GuestClockTouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
    }

protected:
    void OnTouchReady() override {
        pco_ = &emu_.Get<NecMobilepro900SeriesPcoCompanion>();
        pco_->AttachTouch([this] { SetSampleRate(pco_->ReportRate()); },
                          [this] { ClearOnRestore(); });
    }

    void ApplyHostPen(const HostPen& pen) override {
        if (pen.kind == kPenDown) {
            LOG(Board, "[NEC-TOUCH] pen down %d,%d\n", pen.x, pen.y);
            SendPosition(pen.x, pen.y);
            StartSamples(pco_->ReportRate());
            return;
        }
        LOG(Board, "[NEC-TOUCH] pen up\n");
        StopSamples();
        pco_->SendPenUp();
    }

    void OnSample() override {
        int x = 0;
        int y = 0;
        LatestPosition(&x, &y);
        SendPosition(x, y);
    }

private:
    void SendPosition(int x, int y) {
        uint16_t adc_x = 0u;
        uint16_t adc_y = 0u;
        MapToPanel(x, y, kPanel, &adc_x, &adc_y);
        pco_->SendTouch(adc_x, adc_y);
    }

    NecMobilepro900SeriesPcoCompanion* pco_ = nullptr;
};

}

REGISTER_SERVICE_AS(NecMobilepro900SeriesTouchInput, TouchInput);

}
