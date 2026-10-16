#include "display_size_latch.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

bool DisplaySizeLatch::PublishOnce(CerfEmulator& emu, bool display_enabled) {
    if (published_ || !display_enabled) return false;
    published_ = true;
    emu.Get<HostPresentation>().PanelEnabled();
    return true;
}

void DisplaySizeLatch::SaveState(StateWriter& w) const {
    w.Write<uint8_t>("published", published_ ? 1u : 0u);
}

void DisplaySizeLatch::RestoreState(StateReader& r) {
    uint8_t v = 0;
    r.Read("published", v);
    published_ = v != 0;
}
