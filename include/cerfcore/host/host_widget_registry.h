#pragma once

#include "../core/service.h"
#include "host_widget.h"

#include <mutex>
#include <vector>

namespace cerfcore {

class HostWidgetRegistry : public Service {
public:
    using Service::Service;

    /* Called from a widget's OnReady - possibly after the UI thread is already
       painting, hence the lock. */
    void Register(HostWidget* w);

    /* Snapshot sorted by (Group, WidgetName). */
    std::vector<HostWidget*> Ordered();

    /* Hibernation: persist each widget's guest-visible state. Widgets register
       lazily (on first Get<>), so the set and order can differ between save and
       restore; each entry is keyed by name and length-framed for lookup + skip. */
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    std::mutex               mtx_;
    std::vector<HostWidget*> widgets_;
};

}
