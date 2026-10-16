#include <cerfcore/host/host_widget_registry.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/utf16.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <string>

namespace cerfcore {

REGISTER_SERVICE(HostWidgetRegistry);

void HostWidgetRegistry::Register(HostWidget* w) {
    std::lock_guard<std::mutex> lk(mtx_);
    widgets_.push_back(w);
}

std::vector<HostWidget*> HostWidgetRegistry::Ordered() {
    std::vector<HostWidget*> v;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        v = widgets_;
    }
    std::stable_sort(v.begin(), v.end(), [](HostWidget* a, HostWidget* b) {
        const int ga = static_cast<int>(a->Group());
        const int gb = static_cast<int>(b->Group());
        if (ga != gb) return ga < gb;
        return a->WidgetName() < b->WidgetName();
    });
    return v;
}

void HostWidgetRegistry::SaveState(StateWriter& w) {
    auto ordered = Ordered();
    w.Write<uint32_t>("ordered_count", static_cast<uint32_t>(ordered.size()));
    for (HostWidget* widget : ordered) {
        const std::u16string name = Utf8ToUtf16(widget->WidgetName());
        w.Write<uint32_t>("name_count", static_cast<uint32_t>(name.size()));
        w.WriteBytes("name", name.data(), name.size() * sizeof(char16_t));
        w.BeginFrame(0);
        widget->SaveWidgetState(w);
        w.EndFrame();
    }
}

void HostWidgetRegistry::RestoreState(StateReader& r) {
    auto ordered = Ordered();
    uint32_t n = 0;
    r.Read("ordered_count", n);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t namelen = 0;
        r.Read("name_count", namelen);
        if (namelen > 1024u) r.Reject("widget name of %u characters", namelen);
        std::u16string units(namelen, u'\0');
        r.ReadBytes("name", units.data(), namelen * sizeof(char16_t));
        const std::string name = Utf16ToUtf8(units);
        if (const uint32_t id = r.EnterFrame(); id != 0u)
            r.Reject("widget '%s' body in frame 0x%X, this build writes frame 0", name.c_str(), id);
        size_t target = ordered.size();
        for (size_t k = 0; k < ordered.size(); ++k)
            if (ordered[k]->WidgetName() == name) { target = k; break; }
        if (target == ordered.size()) {
            r.SkipFrame();
            continue;
        }
        ordered[target]->RestoreWidgetState(r);
        r.LeaveFrame();
    }
}

}
