#include "siemens_simatic_mp377_board_io_window.h"

namespace cerfcore {

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377AtuOutboundSecondaryLowPostPowerGuard : public SiemensSimaticMp377BoardIoWindow {
public:
    using SiemensSimaticMp377BoardIoWindow::SiemensSimaticMp377BoardIoWindow;
    uint32_t MmioBase() const override { return kMp377PowerResetEnd; }
    uint32_t MmioSize() const override { return 0xD0200000u - MmioBase(); }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377AtuOutboundSecondaryLowPostPowerGuard);

}
