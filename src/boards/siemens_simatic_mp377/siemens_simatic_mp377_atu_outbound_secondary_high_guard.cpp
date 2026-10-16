#include "siemens_simatic_mp377_board_io_window.h"

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377AtuOutboundSecondaryHighGuard : public SiemensSimaticMp377BoardIoWindow {
public:
    using SiemensSimaticMp377BoardIoWindow::SiemensSimaticMp377BoardIoWindow;
    uint32_t MmioBase() const override { return 0xD0280000u; }
    uint32_t MmioSize() const override { return 0x03D80000u; }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377AtuOutboundSecondaryHighGuard);
