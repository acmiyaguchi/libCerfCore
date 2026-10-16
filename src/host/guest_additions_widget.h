#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/host_widget.h>

#include <string>
#include <vector>

class GuestAdditionsWidget : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    std::string WidgetName() const override { return "Guest Additions"; }
    WidgetGroup Group() const override { return WidgetGroup::GuestAdditions; }
    std::string Tooltip() const override;
    bool TryPrimaryAction() override;
    std::vector<WidgetControl> Controls() override;
    WidgetIcon Icon() const override;
    bool PollDirty() override;

private:
    bool ResizeAvailable() const;
    bool ResizeEnabled() const;

    bool last_drawn_on_ = false;
};
