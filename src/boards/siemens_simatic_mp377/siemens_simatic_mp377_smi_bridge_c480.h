#pragma once

#include "siemens_simatic_mp377_smi_bridge_window.h"

namespace siemens_simatic_mp377 {

class SiemensSimaticMp377SmiBridgeC480 : public SiemensSimaticMp377SmiBridgeWindow {
public:
    using SiemensSimaticMp377SmiBridgeWindow::SiemensSimaticMp377SmiBridgeWindow;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;
    void SaveState(StateWriter& writer) override;
    void RestoreState(StateReader& reader) override;
    void PostRestore() override;
};

} // namespace siemens_mp377
