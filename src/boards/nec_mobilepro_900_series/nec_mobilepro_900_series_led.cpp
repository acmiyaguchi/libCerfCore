#include "nec_mobilepro_900_series_led.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../core/steady_time.h"
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"

namespace cerfcore {

REGISTER_SERVICE(NecMobilepro900SeriesLed);

namespace {
constexpr uint64_t  kBlinkPeriodMs = 500u;
constexpr WidgetRgb kClrLit{120, 255, 140};
constexpr WidgetRgb kClrDark{34, 44, 36};
constexpr WidgetRgb kClrRim{150, 160, 150};
constexpr WidgetRgb kClrBlinkRim{120, 230, 130};
}

bool NecMobilepro900SeriesLed::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
}

void NecMobilepro900SeriesLed::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void NecMobilepro900SeriesLed::SetBlinking(bool blinking) {
    std::lock_guard<std::mutex> lk(mtx_);
    blinking_ = blinking;
}

bool NecMobilepro900SeriesLed::Blinking() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return blinking_;
}

bool NecMobilepro900SeriesLed::LitNow() const {
    return (HostSteadyMicros() / 1000u / kBlinkPeriodMs) % 2u == 0u;
}

std::string NecMobilepro900SeriesLed::Tooltip() const {
    return Blinking() ? "Notification LED - blinking" : "Notification LED - off";
}

WidgetIcon NecMobilepro900SeriesLed::Icon() const {
    constexpr int kR = 6;
    return LedIcon{drawn_lit_ ? kClrLit : kClrDark, drawn_blinking_ ? kClrBlinkRim : kClrRim, kR};
}

bool NecMobilepro900SeriesLed::PollDirty() {
    const bool blinking = Blinking();
    const bool lit      = blinking && LitNow();
    if (blinking == drawn_blinking_ && lit == drawn_lit_) return false;
    drawn_blinking_ = blinking;
    drawn_lit_      = lit;
    return true;
}

}
