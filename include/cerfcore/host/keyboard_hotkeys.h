#pragma once

#include "../core/service.h"
#include "widget_control.h"

#include <vector>

namespace cerfcore {

class KeyboardHotkeys : public Service {
public:
    using Service::Service;
    ~KeyboardHotkeys() override = default;

    using HotkeySection = std::vector<WidgetControl>;

    virtual std::vector<HotkeySection> HotkeySections() = 0;
};

}
