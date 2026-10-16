#include "siemens_simatic_mp377_panel.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "siemens_simatic_mp377_family_id.h"

namespace siemens_simatic_mp377 {
namespace {

class SiemensSimaticMp377Panel12 : public SiemensSimaticMp377Panel {
public:
    using SiemensSimaticMp377Panel::SiemensSimaticMp377Panel;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() ==
               BoardFamilyId::SiemensSimaticMp377;
    }

    SiemensSimaticMp377PanelProfile Profile() const override {
        return SiemensSimaticMp377PanelProfile::Inch12_800x600;
    }
};

}  // namespace

REGISTER_SERVICE_AS_FALLBACK(SiemensSimaticMp377Panel12, SiemensSimaticMp377Panel);

}  // namespace siemens_simatic_mp377
