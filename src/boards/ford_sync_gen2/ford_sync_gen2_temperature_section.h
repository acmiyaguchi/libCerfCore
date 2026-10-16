#pragma once

#include <cerfcore/host/host_widget.h>
#include <cstdint>
#include <string>
#include <vector>

class CerfEmulator;
class StateWriter;
class StateReader;

class TemperatureSection : public WidgetSection {
public:
    explicit TemperatureSection(CerfEmulator& emu) : emu_(emu) {}

    std::string Label() const override;
    std::vector<WidgetControl> BuildControls() override;
    bool PollDirty() override;
    void SaveState(StateWriter& w) const override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr const char* kNoReadingId   = "no_reading";
    static constexpr const char* kNotReportedId = "not_reported";

    CerfEmulator& emu_;
    uint64_t last_revision_ = UINT64_MAX;
    bool imperial_ = false;
};
