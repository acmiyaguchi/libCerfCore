#include <cerfcore/host/pointer_widget.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/utf16.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/host/pointer_host_options.h>
#include <cerfcore/host/pointer_router.h>
#include <cerfcore/host/pointer_source.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

REGISTER_SERVICE(PointerWidget);

bool PointerWidget::ShouldRegister() {
    if (emu_.Get<DeviceConfig>().guest_additions) return true;
    if (emu_.Get<PointerRouter>().Sources().size() > 1) return true;
    const PointerHostOptions* options = emu_.TryGet<PointerHostOptions>();
    return options != nullptr && options->Supported();
}

void PointerWidget::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

std::string PointerWidget::Tooltip() const {
    auto& router = emu_.Get<PointerRouter>();
    PointerSource* active = router.Active();
    std::string tip = active ? active->SourceName() : "Pointing device";
    PointerHostOptions* options = emu_.TryGet<PointerHostOptions>();
    if (router.Sources().size() > 1)                      tip += " - click to switch input device";
    else if (options != nullptr && !options->Controls().empty()) tip += " - click to configure";
    return tip;
}

bool PointerWidget::TryPrimaryAction() {
    auto& router = emu_.Get<PointerRouter>();
    if (router.Sources().size() < 2) return false;
    router.CycleNext();
    return true;
}

std::vector<WidgetControl> PointerWidget::Controls() {
    std::vector<WidgetControl> controls;
    auto& router  = emu_.Get<PointerRouter>();
    auto  sources = router.Sources();
    PointerSource* active = router.Active();
    if (sources.size() > 1) {
        ChoiceControl source;
        source.id = "source";
        for (auto* s : sources) {
            source.options.push_back(ChoiceOption{s->SourceName(), s->SourceName()});
            if (s == active) source.selected = s->SourceName();
        }
        source.select = [this](const std::string& name) {
            auto& r = emu_.Get<PointerRouter>();
            for (auto* s : r.Sources())
                if (s->SourceName() == name) r.SetActive(s);
        };
        controls.push_back(WidgetControl{std::move(source)});
    }

    if (auto* options = emu_.TryGet<PointerHostOptions>()) {
        GroupControl group;
        group.children = options->Controls();
        if (!group.children.empty()) controls.push_back(WidgetControl{std::move(group)});
    }
    return controls;
}

WidgetIcon PointerWidget::Icon() const {
    const char* icon = "ICON_INPUT_GA_POINTER";
    if (auto* a = emu_.Get<PointerRouter>().Active()) icon = a->IconResourceName();
    return ResourceIcon{icon};
}

bool PointerWidget::PollDirty() {
    const PointerSource* a = emu_.Get<PointerRouter>().Active();
    if (a == drawn_source_) return false;
    drawn_source_ = a;
    return true;
}

void PointerWidget::SaveWidgetState(StateWriter& w) const {
    PointerSource* a = emu_.Get<PointerRouter>().Active();
    const std::u16string name = a ? Utf8ToUtf16(a->SourceName()) : std::u16string();
    w.Write<uint32_t>("name_count", static_cast<uint32_t>(name.size()));
    w.WriteBytes("name", name.data(), name.size() * sizeof(char16_t));
    w.Write<uint8_t>("user_picked", emu_.Get<PointerRouter>().UserPicked() ? 1u : 0u);
    if (const PointerHostOptions* options = emu_.TryGet<PointerHostOptions>())
        options->SaveState(w);
}

void PointerWidget::RestoreWidgetState(StateReader& r) {
    uint32_t n = 0;
    r.Read("name_count", n);
    if (n > 1024u) r.Reject("pointer source name of %u characters", n);
    std::u16string name(n, u'\0');
    r.ReadBytes("name", name.data(), n * sizeof(char16_t));
    if (!name.empty()) emu_.Get<PointerRouter>().RestoreActiveByName(Utf16ToUtf8(name));
    uint8_t picked = 0;
    r.Read("user_picked", picked);
    emu_.Get<PointerRouter>().RestoreUserPicked(picked != 0);
    if (auto* options = emu_.TryGet<PointerHostOptions>()) options->RestoreState(r);
}

}
