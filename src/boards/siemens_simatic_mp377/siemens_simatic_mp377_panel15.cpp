#include "siemens_simatic_mp377_panel.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "siemens_mp377_15_daylight_id.h"
#include "siemens_mp377_15_inox_id.h"
#include "siemens_mp377_15_touch_id.h"

namespace cerfcore {

namespace siemens_simatic_mp377 {
namespace {

class SiemensSimaticMp377Panel15 : public SiemensSimaticMp377Panel {
public:
    using SiemensSimaticMp377Panel::SiemensSimaticMp377Panel;

    bool ShouldRegister() override {
        const auto b = emu_.Get<BoardContext>().GetBoardId();
        return b == BoardId::SiemensMp37715Touch || b == BoardId::SiemensMp37715Inox ||
               b == BoardId::SiemensMp37715Daylight;
    }

    SiemensSimaticMp377PanelProfile Profile() const override {
        return SiemensSimaticMp377PanelProfile::Inch15_1024x768;
    }
};

}  // namespace

REGISTER_SERVICE_AS(SiemensSimaticMp377Panel15, SiemensSimaticMp377Panel);

}  // namespace siemens_simatic_mp377

}
