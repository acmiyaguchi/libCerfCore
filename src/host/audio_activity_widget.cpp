#include "audio_activity_widget.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>

namespace cerfcore {

void AudioActivityWidget::NotePresent() {
    if (registered_) return;
    registered_ = true;
    emu_.Get<HostWidgetRegistry>().Register(this);
}

REGISTER_SERVICE(AudioActivityWidget);

}
