#include "siemens_simatic_mp377_ertec400_bar_window.h"
#include "siemens_simatic_mp377_ertec400.h"
#include "siemens_simatic_mp377_ertec400_model.h"
#include "siemens_simatic_mp377_ertec400_nrt.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {
class SiemensSimaticMp377Ertec400SmallBars final : public SiemensSimaticMp377Ertec400BarWindow {
public:
    using SiemensSimaticMp377Ertec400BarWindow::SiemensSimaticMp377Ertec400BarWindow;
    uint32_t MmioBase() const override { return siemens_simatic_mp377::kErtecSmallWindowBase; }
    uint32_t MmioSize() const override { return siemens_simatic_mp377::kErtecSmallWindowSize; }

    void SaveState(StateWriter& writer) override {
        emu_.Get<SiemensSimaticMp377Ertec400Model>().SaveState(writer);
        emu_.Get<SiemensSimaticMp377Ertec400Nrt>().SaveState(writer);
    }
    void RestoreState(StateReader& reader) override {
        emu_.Get<SiemensSimaticMp377Ertec400Model>().RestoreState(reader);
        emu_.Get<SiemensSimaticMp377Ertec400Nrt>().RestoreState(reader);
    }
    void PostRestore() override { emu_.Get<SiemensSimaticMp377Ertec400Nrt>().PostRestore(); }

};
} // namespace
REGISTER_SERVICE(SiemensSimaticMp377Ertec400SmallBars);

}
