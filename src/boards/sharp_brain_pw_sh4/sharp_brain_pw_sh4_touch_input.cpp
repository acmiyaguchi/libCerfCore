#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/touch_input.h>
#include "../../execution/engine_bridge.h"
#include "sharp_brain_pw_sh4_family_id.h"
#include "sharp_brain_pw_sh4_touch_panel.h"
#include <cerfengine/host_request_channel.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 default.reg HKLM\HARDWARE\DEVICEMAP\TOUCH CalibrationData. */
constexpr uint32_t kPoints = 5u;
constexpr double   kCalRaw[kPoints][2] = {
    {1931.0, 1961.0}, {889.0, 2993.0}, {888.0, 906.0}, {2961.0, 920.0}, {2958.0, 3039.0},
};

/* sharp_brain_pw_sh4 ddraw_mx28.dll GetModeInfo c06d50c0 800x480; sx8650_touchscreen.dll
   TPD_GetCalPoint c07c20ec targets from cx / 10 and cy / 10. */
constexpr int kScreenW = 800;
constexpr int kScreenH = 480;

/* Section 38.5.6 CHn VALUE: 12-bit result. */
constexpr double kCodeMax = 4095.0;

struct Pen {
    bool down;
    int  x;
    int  y;
};

double Det3(const double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

class SharpBrainPwSh4TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        panel_ = &emu_.Get<SharpBrainPwSh4TouchPanel>();
        host_requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
        host_requests_->RegisterListener([this] { DrainPens(); });
        FitCalibration();
    }

    void OnPenDown(int x, int y) override { Queue(Pen{true, x, y}); }
    void OnPenMove(int x, int y) override { Queue(Pen{true, x, y}); }
    void OnPenUp(int x, int y) override { Queue(Pen{false, x, y}); }
    void OnCaptureLost() override { Queue(Pen{false, 0, 0}); }

private:
    void Queue(const Pen& pen) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pens_.push_back(pen);
        }
        host_requests_->Request();
    }

    void DrainPens() {
        std::vector<Pen> pens;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pens.swap(pens_);
        }
        for (const Pen& p : pens) {
            if (!p.down) {
                panel_->SetContact(false, 0u, 0u);
                continue;
            }
            panel_->SetContact(true, Raw(0u, p.x, p.y), Raw(1u, p.x, p.y));
        }
    }

    /* sharp_brain_pw_sh4 sx8650_touchscreen.dll TouchPanelSetCalibration c07c2f68: least-squares
       affine fit; inverted here as raw = a * x + b * y + c. */
    void FitCalibration() {
        const int dx = kScreenW / 10;
        const int dy = kScreenH / 10;
        const int target[kPoints][2] = {
            {kScreenW / 2, kScreenH / 2},
            {dx * 2, dy * 2},
            {dx * 2, kScreenH - dy * 2},
            {kScreenW - dx * 2, kScreenH - dy * 2},
            {kScreenW - dx * 2, dy * 2},
        };
        double m[3][3] = {};
        double v[2][3] = {};
        for (uint32_t i = 0; i < kPoints; ++i) {
            const double row[3] = {static_cast<double>(target[i][0]),
                                   static_cast<double>(target[i][1]), 1.0};
            for (uint32_t r = 0; r < 3u; ++r) {
                for (uint32_t c = 0; c < 3u; ++c) m[r][c] += row[r] * row[c];
                for (uint32_t a = 0; a < 2u; ++a) v[a][r] += row[r] * kCalRaw[i][a];
            }
        }
        const double det = Det3(m);
        for (uint32_t a = 0; a < 2u; ++a) {
            for (uint32_t k = 0; k < 3u; ++k) {
                double mk[3][3];
                for (uint32_t r = 0; r < 3u; ++r) {
                    for (uint32_t c = 0; c < 3u; ++c) mk[r][c] = c == k ? v[a][r] : m[r][c];
                }
                coef_[a][k] = Det3(mk) / det;
            }
        }
    }

    uint32_t Raw(uint32_t axis, int x, int y) const {
        const double raw = coef_[axis][0] * x + coef_[axis][1] * y + coef_[axis][2];
        if (raw <= 0.0) return 0u;
        if (raw >= kCodeMax) return static_cast<uint32_t>(kCodeMax);
        return static_cast<uint32_t>(raw + 0.5);
    }

    SharpBrainPwSh4TouchPanel*      panel_         = nullptr;
    cerfengine::HostRequestChannel* host_requests_ = nullptr;
    std::mutex                      mtx_;
    std::vector<Pen>                pens_;
    double                          coef_[2][3]    = {};
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4TouchInput, TouchInput);

}
