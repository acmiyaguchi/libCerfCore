#include "siemens_simatic_mp377_board_io_window.h"

namespace cerfcore {

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377FfBandLowGuard : public SiemensSimaticMp377BoardIoWindow {
public:
    using SiemensSimaticMp377BoardIoWindow::SiemensSimaticMp377BoardIoWindow;
    uint32_t MmioBase() const override { return 0xFF000000u; }
    uint32_t MmioSize() const override { return 0x00D80000u; }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377FfBandLowGuard);

}
