#include "siemens_simatic_mp377_panel.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "siemens_mp377_19_touch_id.h"

namespace siemens_simatic_mp377 {
namespace {

class SiemensSimaticMp377Panel19 : public SiemensSimaticMp377Panel {
public:
    using SiemensSimaticMp377Panel::SiemensSimaticMp377Panel;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardId() == BoardId::SiemensMp37719Touch;
    }

    SiemensSimaticMp377PanelProfile Profile() const override {
        return SiemensSimaticMp377PanelProfile::Inch19_1280x1024;
    }
};

}  // namespace

REGISTER_SERVICE_AS(SiemensSimaticMp377Panel19, SiemensSimaticMp377Panel);

}  // namespace siemens_simatic_mp377
