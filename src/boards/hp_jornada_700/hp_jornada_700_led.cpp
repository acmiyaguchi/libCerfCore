#include "hp_jornada_700_led.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../core/steady_time.h"
#include <cerfcore/host/host_widget_registry.h>
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"

REGISTER_SERVICE(HpJornada700Led);

namespace {
constexpr uint64_t  kBlinkPeriodMs = 500;
constexpr WidgetRgb kClrLit{120, 255, 140};
constexpr WidgetRgb kClrDark{34, 44, 36};
constexpr WidgetRgb kClrRim{150, 160, 150};
}  /* namespace */

bool HpJornada700Led::LitNow(State s) {
    return s == State::On ||
           (s == State::Blink && (HostSteadyMicros() / 1000u / kBlinkPeriodMs) % 2u == 0u);
}

bool HpJornada700Led::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void HpJornada700Led::OnReady() {
    /* Button line idles high; a press pulls it low (falling edge, GFER13). */
    emu_.Get<Sa11xxGpio>().DriveInputPin(13, /*level=*/true);
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void HpJornada700Led::SetState(State s) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    state_ = s;
}

uint8_t HpJornada700Led::StatusByte() const {
    switch (CurrentState()) {
        case State::On:    return 0x01;
        case State::Blink: return 0x03;
        default:           return 0x00;
    }
}

HpJornada700Led::State HpJornada700Led::CurrentState() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return state_;
}

void HpJornada700Led::PressButton() {
    auto& gpio = emu_.Get<Sa11xxGpio>();
    gpio.DriveInputPin(13, false);
    gpio.DriveInputPin(13, true);
}

std::string HpJornada700Led::Tooltip() const {
    switch (CurrentState()) {
        case State::On:    return "Notification LED - on (click = press button)";
        case State::Blink: return "Notification LED - blinking (click = press button)";
        default:           return "Notification LED - off (click = press button)";
    }
}

std::vector<WidgetControl> HpJornada700Led::Controls() {
    ActionControl press;
    press.id    = "press_button";
    press.label = "Press notification button";
    press.press = [this] { PressButton(); };
    return {WidgetControl{std::move(press)}};
}

WidgetIcon HpJornada700Led::Icon() const {
    constexpr int kR = 6;
    return LedIcon{LitNow(CurrentState()) ? kClrLit : kClrDark, kClrRim, kR};
}

bool HpJornada700Led::PollDirty() {
    const State s   = CurrentState();
    const bool  lit = LitNow(s);
    if (s == last_drawn_state_ && lit == last_drawn_lit_) return false;
    last_drawn_state_ = s;
    last_drawn_lit_   = lit;
    return true;
}
