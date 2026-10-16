#include "siemens_simatic_mp377_board_io_window.h"

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377AtuOutboundSecondaryLowPreAspc2Guard : public SiemensSimaticMp377BoardIoWindow {
public:
    using SiemensSimaticMp377BoardIoWindow::SiemensSimaticMp377BoardIoWindow;
    uint32_t MmioBase() const override { return kMp377MramBase + kMp377MramSize; }
    uint32_t MmioSize() const override { return kMp377Aspc2Base - MmioBase(); }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377AtuOutboundSecondaryLowPreAspc2Guard);
