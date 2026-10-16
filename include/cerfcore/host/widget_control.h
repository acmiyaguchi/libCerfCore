#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct WidgetControl;

struct InfoControl {
    std::string text;
};

struct LinkControl {
    std::string label;
    std::string url;
};

struct ActionControl {
    std::string           id;
    std::string           label;
    bool                  enabled = true;
    std::function<void()> press;
};

struct ToggleControl {
    std::string               id;
    std::string               label;
    bool                      value   = false;
    bool                      enabled = true;
    std::function<void(bool)> set;
};

struct ChoiceOption {
    std::string id;
    std::string label;
};

struct ChoiceControl {
    std::string                             id;
    std::vector<ChoiceOption>               options;
    std::string                             selected;
    std::function<void(const std::string&)> select;
};

enum class SlotKind : uint8_t { kPcCard, kSerialPort, kUsbDisk, kSdCard };

struct SlotEntry {
    std::string                id;
    std::string                label;
    bool                       enabled = true;
    std::function<void()>      insert;
    std::vector<SlotEntry>     variants;
    std::vector<WidgetControl> details;
};

struct SlotControl {
    std::string                 id;
    SlotKind                    kind = SlotKind::kPcCard;
    std::optional<ChoiceOption> inserted;
    std::vector<SlotEntry>      available;
    std::function<void()>       eject;
    std::vector<WidgetControl>  inserted_controls;
};

struct GroupControl {
    std::string                label;
    std::vector<WidgetControl> children;
};

struct WidgetControl {
    std::variant<InfoControl, LinkControl, ActionControl, ToggleControl, ChoiceControl,
                 SlotControl, GroupControl>
        control;
};
