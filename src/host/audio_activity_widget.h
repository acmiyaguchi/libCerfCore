#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/host_widget.h>

#include <string>

namespace cerfcore {

/* Status-bar audio-activity indicator shared by every sound system: MarkTx on
   sound output (green dot), MarkRx on microphone capture (red dot). */
class AudioActivityWidget : public Service, public HostWidget {
public:
    using Service::Service;

    /* Idempotent: a sound backend declares it exists, registering the indicator
       with the status bar on the first call. */
    void NotePresent();

    std::string WidgetName() const override { return "Audio"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }
    std::string Tooltip() const override {
        return "Audio - TX: sound output, RX: microphone";
    }

    WidgetIcon Icon() const override { return ResourceIcon{"speaker"}; }

private:
    bool registered_ = false;
};

}
