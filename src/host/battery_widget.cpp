#include "battery_widget.h"

#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr int kLevelHigh = 65;
constexpr int kLevelLow  = 20;

}  /* namespace */

BatteryLevel BatteryWidget::LevelOf(int fill) {
    return fill >= kLevelHigh ? BatteryLevel::kHigh
         : fill >= kLevelLow  ? BatteryLevel::kLow
                              : BatteryLevel::kCritical;
}

int BatteryWidget::FillPercent() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return fill_percent_;
}

bool BatteryWidget::IsOnBattery() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return on_battery_;
}

void BatteryWidget::SetOnBattery(bool on_battery) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        on_battery_ = on_battery;
    }
    if (on_change_) on_change_();
}

void BatteryWidget::SetFillPercent(int fill) {
    if (fill < 0)   fill = 0;
    if (fill > 100) fill = 100;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        fill_percent_ = fill;
    }
    if (on_change_) on_change_();
}

std::string BatteryWidget::Tooltip() const {
    bool on_batt;
    int  fill;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        on_batt = on_battery_;
        fill    = fill_percent_;
    }
    const BatteryLevel lvl = LevelOf(fill);
    const char* level = lvl == BatteryLevel::kHigh ? "High"
                      : lvl == BatteryLevel::kLow  ? "Low"
                                                   : "Critical";
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Battery - %s, %d%% (%s)",
                  on_batt ? "on battery" : "on AC", fill, level);
    return buf;
}

WidgetIcon BatteryWidget::Icon() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return BatteryIcon{fill_percent_, on_battery_, LevelOf(fill_percent_)};
}

std::vector<WidgetControl> BatteryWidget::Controls() {
    bool on_batt;
    int  fill;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        on_batt = on_battery_;
        fill    = fill_percent_;
    }

    ToggleControl ac;
    ac.id    = "on_battery";
    ac.label = "On battery power";
    ac.value = on_batt;
    ac.set   = [this](bool on) { SetOnBattery(on); };

    ChoiceControl level;
    level.id = "charge_level";
    for (int preset : { 100, 80, 60, 40, 20, 5 }) {
        const std::string id = std::to_string(preset);
        level.options.push_back(ChoiceOption{id, id + "%"});
        if (fill == preset) level.selected = id;
    }
    level.select = [this](const std::string& id) { SetFillPercent(std::stoi(id)); };

    return {
        WidgetControl{GroupControl{"", {WidgetControl{std::move(ac)}}}},
        WidgetControl{GroupControl{"Charge level", {WidgetControl{std::move(level)}}}},
    };
}

bool BatteryWidget::PollDirty() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const uint8_t on = on_battery_ ? 1u : 0u;
    if (on == last_drawn_on_battery_ && fill_percent_ == last_drawn_fill_) {
        return false;
    }
    last_drawn_on_battery_ = on;
    last_drawn_fill_       = fill_percent_;
    return true;
}

void BatteryWidget::SaveWidgetState(StateWriter& w) const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    w.Write<uint8_t>("on_battery", on_battery_ ? 1u : 0u);
    w.Write<int32_t>("fill_percent", fill_percent_);
}

void BatteryWidget::RestoreWidgetState(StateReader& r) {
    uint8_t on = 0;
    int32_t fill = 100;
    r.Read("on_battery", on);
    r.Read("fill_percent", fill);
    if (fill < 0)   fill = 0;
    if (fill > 100) fill = 100;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        on_battery_   = (on != 0u);
        fill_percent_ = fill;
    }
    /* Re-apply onto the owner's hardware model (GPIO/MCU lines), outside the
       lock since the handler re-reads the getters. Pull-based owners have no
       handler and read the restored state live. */
    if (on_change_) on_change_();
}
