#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_hotkeys.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"
#include "hp_jornada_820_keyboard.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

struct KeyEntry { const char* label; uint8_t vk; };

/* Physical J820 app-key row, left-to-right (device-owner photo; 0xC1 Inbox /
   0xC2 IE / 0xC3 Calendar runtime-confirmed). They ride the normal scancode
   path. NOT the J720 row - copying that mislabels every hardware key. */
constexpr KeyEntry kAppRow[] = {
    { "Inbox",        0xC1 }, { "Internet Explorer", 0xC2 },
    { "Calendar",     0xC3 }, { "Contacts",          0xC4 },
    { "Tasks",        0xC5 }, { "Pocket Word",       0xC6 },
    { "Pocket Excel", 0xC7 }, { "Pocket PowerPoint", 0xC8 },
    { "HP viewer",    0xC9 }, { "Voice Recorder",    0xCA },
    { "HP settings",  0xCB },
};

class HpJornada820KeyboardHotkeys : public KeyboardHotkeys {
public:
    using KeyboardHotkeys::KeyboardHotkeys;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada820;
    }

    std::vector<HotkeySection> HotkeySections() override {
        HotkeySection sec;
        for (const KeyEntry& k : kAppRow) {
            ActionControl it;
            it.id    = "key:" + std::to_string(k.vk);
            it.label = k.label;
            it.press = [this, vk = k.vk] {
                auto& kbd = emu_.Get<HpJornada820Keyboard>();
                kbd.OnHostKey(vk, /*key_up=*/false);
                kbd.OnHostKey(vk, /*key_up=*/true);
            };
            sec.push_back(WidgetControl{std::move(it)});
        }
        return { std::move(sec) };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada820KeyboardHotkeys, KeyboardHotkeys);
