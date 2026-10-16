#include "siemens_simatic_mp377_board_io_window.h"

namespace cerfcore {

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377FfBandHighGuard : public SiemensSimaticMp377BoardIoWindow {
public:
    using SiemensSimaticMp377BoardIoWindow::SiemensSimaticMp377BoardIoWindow;
    uint32_t MmioBase() const override { return 0xFFE80000u; }
    uint32_t MmioSize() const override { return 0x0017F000u; }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377FfBandHighGuard);

}
