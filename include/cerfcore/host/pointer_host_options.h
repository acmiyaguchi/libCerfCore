#pragma once

#include "../core/service.h"
#include "widget_control.h"

#include <vector>

namespace cerfcore {

class StateWriter;
class StateReader;

class PointerHostOptions : public Service {
public:
    using Service::Service;

    virtual bool Supported() const = 0;
    virtual std::vector<WidgetControl> Controls() = 0;
    virtual void SaveState(StateWriter& w) const = 0;
    virtual void RestoreState(StateReader& r) = 0;
};

}
