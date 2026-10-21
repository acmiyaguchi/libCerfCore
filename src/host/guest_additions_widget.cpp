#include "guest_additions_widget.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/host/customizations_editor.h>
#include <cerfcore/host/guest_resize_policy.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/host/task_manager_view.h>

namespace cerfcore {

REGISTER_SERVICE(GuestAdditionsWidget);

bool GuestAdditionsWidget::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void GuestAdditionsWidget::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

bool GuestAdditionsWidget::ResizeAvailable() const {
    const GuestResizePolicy* policy = emu_.TryGet<GuestResizePolicy>();
    return policy != nullptr && policy->Available();
}

bool GuestAdditionsWidget::ResizeEnabled() const {
    const GuestResizePolicy* policy = emu_.TryGet<GuestResizePolicy>();
    return policy != nullptr && policy->Enabled();
}

std::string GuestAdditionsWidget::Tooltip() const {
    if (!ResizeAvailable()) return "Guest Additions - click for tools";
    return ResizeEnabled()
        ? "Guest Additions - auto-resize ON (click to disable, right-click for tools)"
        : "Guest Additions - auto-resize OFF (click to enable, right-click for tools)";
}

bool GuestAdditionsWidget::TryPrimaryAction() {
    if (!ResizeAvailable()) return false;
    auto& policy = emu_.Get<GuestResizePolicy>();
    policy.SetEnabled(!policy.Enabled());
    return true;
}

std::vector<WidgetControl> GuestAdditionsWidget::Controls() {
    GroupControl actions;

    if (emu_.TryGet<TaskManagerView>()) {
        ActionControl taskmgr;
        taskmgr.id    = "task_manager";
        taskmgr.label = "Task manager…";
        taskmgr.press = [this] { emu_.Get<TaskManagerView>().Show(); };
        actions.children.push_back(WidgetControl{std::move(taskmgr)});
    }

    if (emu_.TryGet<CustomizationsEditor>()) {
        ActionControl chres;
        chres.id    = "customizations";
        chres.label = "Resolution / Customizations…";
        chres.press = [this] { emu_.Get<CustomizationsEditor>().Open(); };
        actions.children.push_back(WidgetControl{std::move(chres)});
    }

    if (ResizeAvailable()) {
        ToggleControl resize;
        resize.id    = "resize_to_window";
        resize.label = "Resize guest to window";
        resize.value = ResizeEnabled();
        resize.set   = [this](bool on) { emu_.Get<GuestResizePolicy>().SetEnabled(on); };
        actions.children.push_back(WidgetControl{std::move(resize)});
    }

    std::vector<WidgetControl> controls{WidgetControl{InfoControl{"CERF Guest Additions"}}};
    if (!actions.children.empty()) controls.push_back(WidgetControl{std::move(actions)});
    return controls;
}

WidgetIcon GuestAdditionsWidget::Icon() const {
    return ResourceIcon{ResizeEnabled() ? "ga_autoresize" : "ga_autoresize_disabled"};
}

bool GuestAdditionsWidget::PollDirty() {
    const bool on = ResizeEnabled();
    if (on == last_drawn_on_) return false;
    last_drawn_on_ = on;
    return true;
}

}
