#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/host_widget.h>

#include <mutex>

namespace cerfcore {

class NecMobilepro900SeriesLed : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetBlinking(bool blinking);
    bool Blinking() const;

    std::string WidgetName() const override { return "Notification LED"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }
    std::string Tooltip() const override;
    WidgetIcon  Icon() const override;
    bool        PollDirty() override;

private:
    bool LitNow() const;

    mutable std::mutex mtx_;
    bool               blinking_ = false;

    bool drawn_blinking_ = false;
    bool drawn_lit_      = false;
};

}
