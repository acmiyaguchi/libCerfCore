#pragma once

#include "../core/service.h"
#include "host_widget.h"

#include <string>
#include <vector>

class PointerSource;

class PointerWidget : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    std::string WidgetName() const override { return "Pointing device"; }
    WidgetGroup Group() const override { return WidgetGroup::InputControl; }
    std::string Tooltip() const override;
    bool TryPrimaryAction() override;
    std::vector<WidgetControl> Controls() override;
    WidgetIcon Icon() const override;
    bool PollDirty() override;
    void SaveWidgetState(StateWriter& w) const override;
    void RestoreWidgetState(StateReader& r) override;

private:
    const PointerSource* drawn_source_ = nullptr;
};
