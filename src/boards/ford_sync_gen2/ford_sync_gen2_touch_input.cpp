#include <cerfcore/host/host_presentation.h>
#include <cerfcore/host/touch_input.h>
#include "../../peripherals/ti_tsc2003/ti_tsc2003_touch.h"
#include "../../socs/imx51/imx51_gpio1.h"
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"

#include <cerfcore/core/cerf_emulator.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cerfcore {

namespace {

/* TSC2003 PENIRQ is wired to GPIO1.1, active-low (touch_tsc2003.dll TSCTouchInit
   sub_C14718F8: DDKGpioSetConfig(port 0, pin 1); DDK port 0 = GPIO1). */
constexpr uint32_t kPenGpioPin = 1u;
constexpr long     kAdcMax     = 4095;   /* datasheet: 12-bit result */

/* Inverse of the guest touch calibration: TouchPanelSetCalibration (0xC147327C)
   fits screen=F(raw) from CalibrationData + the cap=2 crosshairs, applied per sample
   by TouchPanelCalibrateAPoint (0xC1473094); CERF emits raw=F^-1(pixel). */
constexpr int kCalibW = 800;   /* the 800x480 panel the CalibrationData was captured on */
constexpr int kCalibH = 480;
/* CalibrationData = "2016,2008 3167,3153 3193,872 876,856 856,3104"
   (identical in NK.bin and AppFS default.hv), in crosshair index order 0..4. */
constexpr int kRawPt[5][2] = {
    {2016, 2008}, {3167, 3153}, {3193, 872}, {876, 856}, {856, 3104}};

/* Crosshair screen position for index (sub_C1472D1C), integer arithmetic:
   0 (W/2,H/2); 1 (2*(W/10),2*(H/10)); 2 (2*(W/10),H-2*(H/10));
   3 (W-2*(W/10),H-2*(H/10)); 4 (W-2*(W/10),2*(H/10)). */
int CrosshairX(int idx, int w) {
    const int in = 2 * (w / 10);
    switch (idx) {
        case 1: case 2: return in;
        case 3: case 4: return w - in;
        default:        return w / 2;
    }
}
int CrosshairY(int idx, int h) {
    const int in = 2 * (h / 10);
    switch (idx) {
        case 1: case 4: return in;
        case 2: case 3: return h - in;
        default:        return h / 2;
    }
}

/* Solve the 3x3 system m*x=b (Gaussian elimination, partial pivot). */
bool Solve3(double m[3][3], const double b[3], double out[3]) {
    double a[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) a[i][j] = m[i][j];
        a[i][3] = b[i];
    }
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (std::fabs(a[piv][col]) < 1e-12) return false;
        for (int j = 0; j < 4; ++j) std::swap(a[col][j], a[piv][j]);
        const double pv = a[col][col];
        for (int j = col; j < 4; ++j) a[col][j] /= pv;
        for (int r = 0; r < 3; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            for (int j = col; j < 4; ++j) a[r][j] -= f * a[col][j];
        }
    }
    for (int i = 0; i < 3; ++i) out[i] = a[i][3];
    return true;
}

class FordSyncGen2TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }

    void OnReady() override {
        ComputeInverseCalibration();
        emu_.Get<Imx51Gpio1>().SetInputPin(kPenGpioPin, true);   /* PENIRQ idle high */
        emu_.Get<Tsc2003Touch>().SetPenIrqObserver([this](bool asserted) {
            emu_.Get<Imx51Gpio1>().SetInputPin(kPenGpioPin, !asserted);
        });
    }

    void OnPenDown    (int x, int y) override { Apply(x, y, true);  }
    void OnPenMove    (int x, int y) override { Apply(x, y, true);  }
    void OnPenUp      (int x, int y) override { Apply(x, y, false); }
    void OnCaptureLost()             override {
        emu_.Get<Tsc2003Touch>().SetState(0, 0, false);
    }

private:
    /* Build F (raw->screen) as the LS affine the OS builds from the 5 crosshair/raw
       pairs, then invert it to G (screen->raw) once. */
    void ComputeInverseCalibration() {
        double ata[3][3] = {{0}};
        double atx[3] = {0};   /* normal-eq RHS for screen-X */
        double aty[3] = {0};   /* ... screen-Y */
        for (int i = 0; i < 5; ++i) {
            const double r[3] = {static_cast<double>(kRawPt[i][0]),
                                 static_cast<double>(kRawPt[i][1]), 1.0};
            const double sx = CrosshairX(i, kCalibW);
            const double sy = CrosshairY(i, kCalibH);
            for (int a = 0; a < 3; ++a) {
                atx[a] += r[a] * sx;
                aty[a] += r[a] * sy;
                for (int b = 0; b < 3; ++b) ata[a][b] += r[a] * r[b];
            }
        }
        double f1[3], f2[3];   /* F: sx=f1.(rx,ry,1); sy=f2.(rx,ry,1) */
        double t1[3][3], t2[3][3];
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) { t1[a][b] = ata[a][b]; t2[a][b] = ata[a][b]; }
        if (!Solve3(t1, atx, f1) || !Solve3(t2, aty, f2)) {
            calibrated_ = false;
            return;
        }
        const double A = f1[0], B = f1[1], C = f1[2];
        const double D = f2[0], E = f2[1], Fc = f2[2];
        const double det = A * E - B * D;
        if (std::fabs(det) < 1e-9) { calibrated_ = false; return; }
        const double ia = E / det, ib = -B / det, ic = -D / det, id = A / det;
        gx_[0] = ia; gx_[1] = ib; gx_[2] = -ia * C - ib * Fc;
        gy_[0] = ic; gy_[1] = id; gy_[2] = -ic * C - id * Fc;
        calibrated_ = true;
    }

    void Apply(int host_x, int host_y, bool pen_down) {
        auto& hc = emu_.Get<HostPresentation>();
        const uint32_t sw = hc.SurfaceWidth();
        const uint32_t sh = hc.SurfaceHeight();
        /* Map the guest-surface pixel into the 800x480 calibration basis. */
        const double px = sw > 0u ? host_x * static_cast<double>(kCalibW) / sw : host_x;
        const double py = sh > 0u ? host_y * static_cast<double>(kCalibH) / sh : host_y;
        const double rx = gx_[0] * px + gx_[1] * py + gx_[2];
        const double ry = gy_[0] * px + gy_[1] * py + gy_[2];
        const auto adc_x = static_cast<uint16_t>(
            std::clamp<long>(std::lround(rx), 0, kAdcMax));
        const auto adc_y = static_cast<uint16_t>(
            std::clamp<long>(std::lround(ry), 0, kAdcMax));
        emu_.Get<Tsc2003Touch>().SetState(adc_x, adc_y, pen_down);
    }

    bool   calibrated_ = false;
    double gx_[3] = {0, 0, 0};   /* rawX = gx_[0]*px + gx_[1]*py + gx_[2] */
    double gy_[3] = {0, 0, 0};   /* rawY = gy_[0]*px + gy_[1]*py + gy_[2] */
};

}  /* namespace */

REGISTER_SERVICE_AS(FordSyncGen2TouchInput, TouchInput);

}
