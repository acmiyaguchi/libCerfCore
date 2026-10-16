#include "compaq_ipaq_h3000_panel.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_ipaq_h3000_family_id.h"

namespace cerfcore {

namespace {

class CompaqIpaqH3000TftPanel : public CompaqIpaqH3000Panel {
public:
    using CompaqIpaqH3000Panel::CompaqIpaqH3000Panel;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() ==
               BoardFamilyId::CompaqIpaqH3000;
    }

    /* Linux arch/arm/mach-sa1100/h3600.c h3600_lcd_info:
       .lccr0 = LCCR0_Color | LCCR0_Sngl | LCCR0_Act. */
    Kind PanelKind() const override { return Kind{ true, false }; }
};

}

REGISTER_SERVICE_AS_FALLBACK(CompaqIpaqH3000TftPanel, CompaqIpaqH3000Panel);

}
