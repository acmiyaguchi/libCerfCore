#include "siemens_simatic_mp377_board_io_window.h"

namespace cerfcore {

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377AtuOutboundSecondaryPostAspc2PreRamGuard : public SiemensSimaticMp377BoardIoWindow {
public:
    using SiemensSimaticMp377BoardIoWindow::SiemensSimaticMp377BoardIoWindow;
    uint32_t MmioBase() const override { return kMp377Aspc2Base + kMp377Aspc2Size; }
    uint32_t MmioSize() const override { return kMp377Aspc2RamPa - MmioBase(); }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377AtuOutboundSecondaryPostAspc2PreRamGuard);

}
