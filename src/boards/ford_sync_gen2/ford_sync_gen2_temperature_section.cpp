#include "ford_sync_gen2_temperature_section.h"

#include "ford_sync_gen2_ambient_temperature.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include <cstdio>

namespace cerfcore {

std::string TemperatureSection::Label() const {
    const auto snapshot = emu_.Get<FordSyncGen2AmbientTemperature>().Read();
    if (!snapshot.available) return "Outside temperature: Not reported";
    if (!snapshot.half_celsius) return "Outside temperature: No reading";
    char text[64];
    if (imperial_) {
        const double fahrenheit = *snapshot.half_celsius * 0.9 + 32.0;
        std::snprintf(text, std::size(text), "Outside temperature: %.1f \u00b0F", fahrenheit);
    } else {
        std::snprintf(text, std::size(text), "Outside temperature: %.1f \u00b0C", *snapshot.half_celsius * 0.5);
    }
    return text;
}

std::vector<WidgetControl> TemperatureSection::BuildControls() {
    /* EA5T-14D544-BA.sec, VNIGeneralSvc.dll sub_C1595D40, Fahrenheit table C15AA340. */
    constexpr int16_t celsius[] = {-40, -30, -20, -10, -5, 0, 5, 10, 15, 20, 25, 30, 35, 40, 50};
    constexpr int16_t fahrenheit[] = {-40, -22, -4, 14, 23, 32, 41, 50, 59, 68, 77, 86, 95, 104, 122};
    const auto snapshot = emu_.Get<FordSyncGen2AmbientTemperature>().Read();
    ChoiceControl reading;
    reading.id = "outside_temperature";
    for (std::size_t i = 0; i < std::size(celsius); ++i) {
        const std::string id = std::to_string(celsius[i] * 2);
        reading.options.push_back(ChoiceOption{
            id, std::to_string(imperial_ ? fahrenheit[i] : celsius[i]) + (imperial_ ? " °F" : " °C")});
        if (snapshot.available && snapshot.half_celsius == celsius[i] * 2) reading.selected = id;
    }
    reading.options.push_back(ChoiceOption{kNoReadingId, "No reading"});
    reading.options.push_back(ChoiceOption{kNotReportedId, "Stop reporting outside temperature"});
    if (!snapshot.available) reading.selected = kNotReportedId;
    else if (!snapshot.half_celsius) reading.selected = kNoReadingId;
    reading.select = [this](const std::string& id) {
        auto& temperature = emu_.Get<FordSyncGen2AmbientTemperature>();
        if (id == kNoReadingId) temperature.Set(std::nullopt);
        else if (id == kNotReportedId) temperature.Clear();
        else temperature.Set(static_cast<int16_t>(std::stoi(id)));
    };
    ToggleControl units;
    units.id    = "fahrenheit";
    units.label = "Show presets in °F";
    units.value = imperial_;
    units.set   = [this](bool imperial) { imperial_ = imperial; };
    return {WidgetControl{std::move(reading)}, WidgetControl{std::move(units)}};
}

bool TemperatureSection::PollDirty() {
    const auto revision = emu_.Get<FordSyncGen2AmbientTemperature>().Read().revision;
    if (revision == last_revision_) return false;
    last_revision_ = revision;
    return true;
}

void TemperatureSection::SaveState(StateWriter& w) const { w.Write<uint8_t>("imperial", imperial_); }
void TemperatureSection::RestoreState(StateReader& r) {
    uint8_t imperial = 0; r.Read("imperial", imperial); imperial_ = imperial != 0;
}

}
