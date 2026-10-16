#include "siemens_simatic_mp377_board_io_window.h"

using namespace siemens_simatic_mp377_board_io_detail;

namespace {

class SiemensSimaticMp377AtuOutboundPrimaryHighPostErtecGuard : public SiemensSimaticMp377AtuOutboundPrimaryGuard {
public:
    using SiemensSimaticMp377AtuOutboundPrimaryGuard::SiemensSimaticMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return siemens_simatic_mp377::kErtecBar3WindowEnd; }
    uint32_t MmioSize() const override { return kAtuPrimaryEnd - MmioBase(); }

};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377AtuOutboundPrimaryHighPostErtecGuard);
