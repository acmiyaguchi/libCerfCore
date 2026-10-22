#pragma once

#include <cerfcore/core/service.h>
#include "../lcd/panel_scanout.h"

#include <cstdint>

namespace cerfcore {

class StateWriter;
class StateReader;

class MpuLcdPanel : public Service {
public:
    using Service::Service;
    ~MpuLcdPanel() override = default;

    virtual void WriteCommand(uint32_t bus_word) = 0;
    virtual void WriteData(uint32_t bus_word) = 0;

    virtual PanelSurface Gram() const = 0;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};

}
