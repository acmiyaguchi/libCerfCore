#include <cerfcore/core/service.h>
#include <cerfcore/host/host_widget.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/utf16.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/host/keyboard_hotkeys.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_mapping_view.h>
#include <cerfcore/host/keyboard_router.h>
#include <cerfcore/state/state_stream.h>

#include <string>
#include <vector>

namespace cerfcore {

namespace {

class KeyboardWidget : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions ||
               emu_.TryGet<KeyboardMap>() != nullptr;
    }

    void OnReady() override { emu_.Get<HostWidgetRegistry>().Register(this); }

    std::string WidgetName() const override { return "Keyboard"; }
    WidgetGroup Group() const override { return WidgetGroup::InputControl; }
    std::string Tooltip() const override;
    WidgetIcon  Icon() const override;
    std::vector<WidgetControl> Controls() override;
    bool PollDirty() override;

    void SaveWidgetState(StateWriter& w) const override;
    void RestoreWidgetState(StateReader& r) override;

private:
    const KeyboardInput* drawn_source_ = nullptr;
};

std::string KeyboardWidget::Tooltip() const {
    auto& router = emu_.Get<KeyboardRouter>();
    KeyboardInput* active = router.Active();
    std::string tip = active ? active->SourceName() : "Keyboard";
    const bool has_menu = router.Sources().size() > 1 ||
                          emu_.TryGet<KeyboardMap>() != nullptr;
    if (has_menu) tip += " - click to configure input / see key mappings";
    return tip;
}

WidgetIcon KeyboardWidget::Icon() const {
    const char* icon = "ICON_KEYBOARD";
    if (auto* a = emu_.Get<KeyboardRouter>().Active()) icon = a->IconResourceName();
    return ResourceIcon{icon};
}

bool KeyboardWidget::PollDirty() {
    const KeyboardInput* a = emu_.Get<KeyboardRouter>().Active();
    if (a == drawn_source_) return false;
    drawn_source_ = a;
    return true;
}

std::vector<WidgetControl> KeyboardWidget::Controls() {
    std::vector<WidgetControl> controls;

    auto& router  = emu_.Get<KeyboardRouter>();
    auto  sources = router.Sources();
    if (sources.size() > 1) {
        ChoiceControl source;
        source.id = "source";
        KeyboardInput* active = router.Active();
        for (auto* s : sources) {
            source.options.push_back(ChoiceOption{s->SourceName(), s->SourceName()});
            if (s == active) source.selected = s->SourceName();
        }
        source.select = [this](const std::string& name) {
            auto& r = emu_.Get<KeyboardRouter>();
            for (auto* s : r.Sources())
                if (s->SourceName() == name) r.SetActive(s);
        };
        controls.push_back(WidgetControl{std::move(source)});
    }

    if (emu_.TryGet<KeyboardMap>()) {
        if (emu_.TryGet<KeyboardMappingView>()) {
            ActionControl see;
            see.id    = "see_mapping";
            see.label = "See keyboard mapping";
            see.press = [this] { emu_.Get<KeyboardMappingView>().Show(); };
            controls.push_back(WidgetControl{GroupControl{"", {WidgetControl{std::move(see)}}}});
        }

        if (auto* hk = emu_.TryGet<KeyboardHotkeys>()) {
            for (auto& sec : hk->HotkeySections()) {
                if (sec.empty()) continue;
                controls.push_back(WidgetControl{GroupControl{"", std::move(sec)}});
            }
        }
    }
    return controls;
}

void KeyboardWidget::SaveWidgetState(StateWriter& w) const {
    KeyboardInput* a = emu_.Get<KeyboardRouter>().Active();
    const std::u16string name = a ? Utf8ToUtf16(a->SourceName()) : std::u16string();
    w.Write<uint32_t>("name_count", static_cast<uint32_t>(name.size()));
    w.WriteBytes("name", name.data(), name.size() * sizeof(char16_t));
    w.Write<uint8_t>("user_picked", emu_.Get<KeyboardRouter>().UserPicked() ? 1u : 0u);
}

void KeyboardWidget::RestoreWidgetState(StateReader& r) {
    uint32_t n = 0;
    r.Read("name_count", n);
    if (n > 1024u) r.Reject("keyboard source name of %u characters", n);
    std::u16string name(n, u'\0');
    r.ReadBytes("name", name.data(), n * sizeof(char16_t));
    if (!name.empty()) emu_.Get<KeyboardRouter>().RestoreActiveByName(Utf16ToUtf8(name));
    uint8_t picked = 0;
    r.Read("user_picked", picked);
    emu_.Get<KeyboardRouter>().RestoreUserPicked(picked != 0);
}

}

REGISTER_SERVICE(KeyboardWidget);

}
