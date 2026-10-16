#define NOMINMAX

#include "mediaq_mq1188.h"

#include "../mediaq_ge/mediaq_panel_renderer.h"
#include <cerfcore/boards/board_context.h>
#include "../../boards/askey_pc3xx/askey_pc3xx_family_id.h"

namespace {

class MediaQMq1188Renderer : public MediaQPanelRenderer<MediaQMq1188> {
public:
    using MediaQPanelRenderer<MediaQMq1188>::MediaQPanelRenderer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::AskeyPc3xx;
    }
};

}

REGISTER_SERVICE_AS(MediaQMq1188Renderer, PanelFrameRenderer);
