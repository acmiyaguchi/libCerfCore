#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_hotkeys.h>
#include "../../socs/vr41xx/vr41xx_piu.h"
#include <cerfcore/boards/board_context.h>
#include "casio_pocketpostpet_family_id.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

struct StripButton { const char* label; uint16_t adc_y; };

/* touch.dll sub_137095C (Jx627 table @0x1370568): 7 right-strip hard icons, VK 0xE9..0xEF
   top-to-bottom, each record field[2..3] = its v9 (4x GDI px-Y) zone. */
constexpr uint16_t kStripAdcX = 945u;
constexpr StripButton kStrip[] = {
    { "Top Menu",   73u },
    { "Get Mail",  220u },
    { "Internet",  367u },
    { "Mic",       514u },
    { "Tools",     660u },
    { "Set Up",    807u },
    { "Text Edit", 953u },
};

class CasioPocketpostpetKeyboardHotkeys : public KeyboardHotkeys {
public:
    using KeyboardHotkeys::KeyboardHotkeys;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
    }

    std::vector<HotkeySection> HotkeySections() override {
        HotkeySection sec;
        for (const StripButton& b : kStrip) {
            ActionControl it;
            it.id    = "strip:" + std::to_string(b.adc_y);
            it.label = b.label;
            it.press = [this, y = b.adc_y] {
                emu_.Get<Vr41xxPiu>().SyntheticTap(kStripAdcX, y);
            };
            sec.push_back(WidgetControl{std::move(it)});
        }
        return { std::move(sec) };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioPocketpostpetKeyboardHotkeys, KeyboardHotkeys);
