#include "ps2_set2_keymap.h"

namespace {

constexpr Ps2Set2Entry P(uint8_t sc) { return { sc, false }; }
constexpr Ps2Set2Entry E(uint8_t sc) { return { sc, true  }; }
constexpr Ps2Set2Entry N()           { return { 0,  false }; }

constexpr Ps2Set2Entry kVkToPs2Set2[256] = {
    /* 0x00..0x07 */ N(), N(), N(), N(), N(), N(), N(), N(),
    /* 0x08 VK_BACK    */ P(0x66),
    /* 0x09 VK_TAB     */ P(0x0D),
    /* 0x0A..0x0C */ N(), N(), N(),
    /* 0x0D VK_RETURN  */ P(0x5A),
    /* 0x0E..0x0F */ N(), N(),
    /* 0x10 VK_SHIFT   */ P(0x12),
    /* 0x11 VK_CONTROL */ P(0x14),
    /* 0x12 VK_MENU    */ P(0x11),
    /* 0x13 */ N(),
    /* 0x14 VK_CAPITAL */ P(0x58),
    /* 0x15..0x1A */ N(), N(), N(), N(), N(), N(),
    /* 0x1B VK_ESCAPE  */ P(0x76),
    /* 0x1C..0x1F */ N(), N(), N(), N(),
    /* 0x20 VK_SPACE   */ P(0x29),
    /* 0x21 VK_PRIOR   */ E(0x7D),
    /* 0x22 VK_NEXT    */ E(0x7A),
    /* 0x23 VK_END     */ E(0x69),
    /* 0x24 VK_HOME    */ E(0x6C),
    /* 0x25 VK_LEFT    */ E(0x6B),
    /* 0x26 VK_UP      */ E(0x75),
    /* 0x27 VK_RIGHT   */ E(0x74),
    /* 0x28 VK_DOWN    */ E(0x72),
    /* 0x29..0x2C */ N(), N(), N(), N(),
    /* 0x2D VK_INSERT  */ E(0x70),
    /* 0x2E VK_DELETE  */ E(0x71),
    /* 0x2F */ N(),
    /* 0x30..0x39 '0'..'9' */
    P(0x45), P(0x16), P(0x1E), P(0x26), P(0x25),
    P(0x2E), P(0x36), P(0x3D), P(0x3E), P(0x46),
    /* 0x3A..0x40 */ N(), N(), N(), N(), N(), N(), N(),
    /* 0x41..0x5A 'A'..'Z' */
    P(0x1C), P(0x32), P(0x21), P(0x23), P(0x24), P(0x2B), P(0x34),
    P(0x33), P(0x43), P(0x3B), P(0x42), P(0x4B), P(0x3A), P(0x31),
    P(0x44), P(0x4D), P(0x15), P(0x2D), P(0x1B), P(0x2C), P(0x3C),
    P(0x2A), P(0x1D), P(0x22), P(0x35), P(0x1A),
    /* 0x5B..0x5F */ N(), N(), N(), N(), N(),
    /* 0x60..0x69 VK_NUMPAD0..9 */
    P(0x70), P(0x69), P(0x72), P(0x7A), P(0x6B),
    P(0x73), P(0x74), P(0x6C), P(0x75), P(0x7D),
    /* 0x6A VK_MULTIPLY  */ P(0x7C),
    /* 0x6B VK_ADD       */ P(0x79),
    /* 0x6C VK_SEPARATOR */ N(),
    /* 0x6D VK_SUBTRACT  */ P(0x7B),
    /* 0x6E VK_DECIMAL   */ P(0x71),
    /* 0x6F VK_DIVIDE    */ E(0x4A),
    /* 0x70..0x7B F1..F12 */
    P(0x05), P(0x06), P(0x04), P(0x0C), P(0x03), P(0x0B),
    P(0x83), P(0x0A), P(0x01), P(0x09), P(0x78), P(0x07),
    /* 0x7C..0x8B */ N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(), N(), N(),
    /* 0x8C..0x8F */ N(), N(), N(), N(),
    /* 0x90 VK_NUMLOCK */ P(0x77),
    /* 0x91 VK_SCROLL  */ P(0x7E),
    /* 0x92..0x9F */ N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(),
    /* 0xA0..0xB9 */ N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(), N(), N(), N(), N(),
    /* 0xBA VK_OEM_1      ;: */ P(0x4C),
    /* 0xBB VK_OEM_PLUS   =+ */ P(0x55),
    /* 0xBC VK_OEM_COMMA  ,< */ P(0x41),
    /* 0xBD VK_OEM_MINUS  -_ */ P(0x4E),
    /* 0xBE VK_OEM_PERIOD .> */ P(0x49),
    /* 0xBF VK_OEM_2      /? */ P(0x4A),
    /* 0xC0 VK_OEM_3      `~ */ P(0x0E),
    /* 0xC1..0xDA */ N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(), N(), N(), N(), N(),
    /* 0xDB VK_OEM_4      [{ */ P(0x54),
    /* 0xDC VK_OEM_5      \| */ P(0x5D),
    /* 0xDD VK_OEM_6      ]} */ P(0x5B),
    /* 0xDE VK_OEM_7      '" */ P(0x52),
    /* 0xDF..0xFF */ N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(), N(), N(),
                     N(), N(), N(), N(), N(), N(), N(), N(), N(),
};

}  // namespace

const Ps2Set2Entry* Ps2Set2Table() { return kVkToPs2Set2; }

std::vector<KeyBinding> Ps2Set2KeyBindings() {
    std::vector<KeyBinding> bindings;
    for (int vk = 0; vk < 256; ++vk) {
        const Ps2Set2Entry& e = kVkToPs2Set2[vk];
        if (e.scancode == 0) continue;
        const uint32_t code =
            static_cast<uint32_t>(e.scancode) | (e.extended ? 0x100u : 0u);
        bindings.push_back({ static_cast<uint8_t>(vk), code, nullptr, 0, 0 });
    }
    return bindings;
}
