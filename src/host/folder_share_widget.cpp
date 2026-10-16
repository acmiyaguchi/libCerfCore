#include <cerfcore/core/service.h>
#include <cerfcore/host/host_widget.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/folder_share_config.h>
#include <cerfcore/host/customizations_editor.h>
#include <cerfcore/host/host_widget_registry.h>

#include <string>
#include <vector>

namespace cerfcore {

namespace {

class FolderShareWidget : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override { return emu_.Get<DeviceConfig>().guest_additions; }

    void OnReady() override { emu_.Get<HostWidgetRegistry>().Register(this); }

    std::string WidgetName() const override { return "Shared folder"; }
    WidgetGroup Group() const override { return WidgetGroup::Storage; }

    std::string Tooltip() const override {
        auto& cfg = emu_.Get<FolderShareConfig>();
        if (!cfg.Enabled())
            return "Shared folder: off - click to configure";
        return "Shared folder: " + cfg.HostRoot();
    }

    bool ControlsAreOneAction() const override { return true; }

    std::vector<WidgetControl> Controls() override {
        if (emu_.TryGet<CustomizationsEditor>() == nullptr) return {};
        ActionControl configure;
        configure.id    = "configure";
        configure.label = "Configure shared folder…";
        configure.press = [this] { emu_.Get<CustomizationsEditor>().Open(); };
        return {WidgetControl{std::move(configure)}};
    }

    WidgetIcon Icon() const override {
        return FolderIcon{emu_.Get<FolderShareConfig>().Enabled()};
    }

    bool PollDirty() override {
        const bool on = emu_.Get<FolderShareConfig>().Enabled();
        if (on == drawn_enabled_) return false;
        drawn_enabled_ = on;
        return true;
    }

private:
    bool drawn_enabled_ = false;
};

}

REGISTER_SERVICE(FolderShareWidget);

}
