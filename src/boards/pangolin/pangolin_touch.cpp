#include "pangolin_companion_mpu.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "pangolin_family_id.h"
#include <cerfcore/host/host_presentation.h>
#include "../../peripherals/touch/guest_clock_touch_input.h"
#include "../../socs/sa11xx/sa11xx_sp3_uart.h"
#include "../../socs/uart_line_device.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace {

/* smartbook_g138_ce4_2 penmount.dll sub_1B43070, pmode 4, no grid (no CalibPoints, IOCTL 12):
   raw8 = (8 * raw14) & ~0x1F, norm = 26214 * (raw8 - caL) / (caH - caL) + 3276,
   clamped to 0..0x7FFF; registry Drivers\BuiltIn\penmount cax1 / cax2, cay1 / cay2. */
constexpr int kCaxL = 0x6F40, kCaxH = 0x11C0;
constexpr int kCayL = 0x6AC0, kCayH = 0x1660;

constexpr GuestCycleClock::Rate kContactRate{50u, 1u};

uint16_t RawFromPixel(int p, int span, int caL, int caH) {
    if (span < 2) span = 2;
    if (p < 0) p = 0;
    if (p > span - 1) p = span - 1;
    const int norm = p * 32768 / (span - 1);
    int raw8  = caL + (norm - 3276) * (caH - caL) / 26214;
    int c14   = raw8 / 8;
    if (c14 < 0) c14 = 0;
    if (c14 > 0x3FFF) c14 = 0x3FFF;
    return static_cast<uint16_t>(c14);
}

class PangolinTouch : public GuestClockTouchInput, public UartLineDevice {
public:
    using GuestClockTouchInput::GuestClockTouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Pangolin;
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint8_t>("pangolin_touch_down", pen_down_ ? 1u : 0u);
        w.Write("pangolin_touch_x14", last_x14_);
        w.Write("pangolin_touch_y14", last_y14_);
    }

    void RestoreState(StateReader& r) override {
        uint8_t down = 0u;
        r.Read("pangolin_touch_down", down);
        r.Read("pangolin_touch_x14", last_x14_);
        r.Read("pangolin_touch_y14", last_y14_);
        ClearOnRestore();
        pen_down_ = false;
        if (down != 0u) emu_.Get<PangolinCompanionMpu>().SendTouch(false, last_x14_, last_y14_);
    }

protected:
    void OnTouchReady() override { emu_.Get<Sa11xxSp3Uart>().AttachDevice(this); }

    void ApplyHostPen(const HostPen& pen) override {
        const bool down = pen.kind == kPenDown;
        if (down == pen_down_) return;
        pen_down_ = down;
        if (!pen_down_) {
            StopSamples();
            int x = 0;
            int y = 0;
            LatestPosition(&x, &y);
            MapToRaw(x, y);
            emu_.Get<PangolinCompanionMpu>().SendTouch(false, last_x14_, last_y14_);
            return;
        }
        EmitContact(pen.x, pen.y);
        StartSamples(kContactRate);
    }

    void OnSample() override {
        int x = 0;
        int y = 0;
        LatestPosition(&x, &y);
        EmitContact(x, y);
    }

private:
    void MapToRaw(int x_host, int y_host) {
        auto& hc  = emu_.Get<HostPresentation>();
        last_x14_ = RawFromPixel(x_host, static_cast<int>(hc.SurfaceWidth()), kCaxL, kCaxH);
        last_y14_ = RawFromPixel(y_host, static_cast<int>(hc.SurfaceHeight()), kCayL, kCayH);
    }

    void EmitContact(int x_host, int y_host) {
        MapToRaw(x_host, y_host);
        emu_.Get<PangolinCompanionMpu>().SendTouch(true, last_x14_, last_y14_);
    }

    bool     pen_down_ = false;
    uint16_t last_x14_ = 0;
    uint16_t last_y14_ = 0;
};

}

REGISTER_SERVICE_AS(PangolinTouch, TouchInput);

}
