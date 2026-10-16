#include "siemens_simatic_mp377_board_io_window.h"

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377Ebus1HighGuard : public SiemensSimaticMp377BoardIoWindow {
public:
    using SiemensSimaticMp377BoardIoWindow::SiemensSimaticMp377BoardIoWindow;
    uint32_t MmioBase() const override { return siemens_simatic_mp377::kDebugLedTickEnd; }
    uint32_t MmioSize() const override { return kEbus1End - MmioBase(); }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377Ebus1HighGuard);
