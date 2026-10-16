#include "siemens_simatic_mp377_ertec400_bar_window.h"
#include "siemens_simatic_mp377_ertec400.h"

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {
class SiemensSimaticMp377Ertec400Bar3 final : public SiemensSimaticMp377Ertec400BarWindow {
public:
    using SiemensSimaticMp377Ertec400BarWindow::SiemensSimaticMp377Ertec400BarWindow;
    uint32_t MmioBase() const override { return siemens_simatic_mp377::kErtecBar3WindowBase; }
    uint32_t MmioSize() const override { return siemens_simatic_mp377::kErtecBar3WindowSize; }

};
} // namespace
REGISTER_SERVICE(SiemensSimaticMp377Ertec400Bar3);

}
