#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace cerfcore {

struct WidgetRgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct ResourceIcon {
    std::string name;
};

struct ChipIcon {};

struct HardDiskIcon {};

struct LedIcon {
    WidgetRgb fill;
    WidgetRgb rim;
    int       radius  = 0;
    bool      vibrate = false;
};

struct LedGridIcon {
    uint8_t   bits = 0;
    WidgetRgb lit;
    WidgetRgb dark;
    WidgetRgb outline;
};

struct PowerInputIcon {
    bool failed = false;
};

enum class BatteryLevel : uint8_t { kHigh, kLow, kCritical };

struct BatteryIcon {
    int          fill_percent = 0;
    bool         on_battery   = false;
    BatteryLevel level        = BatteryLevel::kHigh;
};

struct FolderIcon {
    bool enabled = false;
};

using WidgetIcon = std::variant<ResourceIcon, ChipIcon, HardDiskIcon, LedIcon, LedGridIcon,
                                PowerInputIcon, BatteryIcon, FolderIcon>;

}
