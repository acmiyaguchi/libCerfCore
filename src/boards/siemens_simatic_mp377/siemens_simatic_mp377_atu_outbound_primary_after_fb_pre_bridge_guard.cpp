#include "siemens_simatic_mp377_board_io_window.h"

namespace cerfcore {

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377AtuOutboundPrimaryAfterFbPreBridgeGuard : public SiemensSimaticMp377AtuOutboundPrimaryGuard {
public:
    using SiemensSimaticMp377AtuOutboundPrimaryGuard::SiemensSimaticMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return kAtuPrimarySm501FbEnd; }
    uint32_t MmioSize() const override { return kAtuPrimaryConsoleBase - MmioBase(); }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377AtuOutboundPrimaryAfterFbPreBridgeGuard);

}
