#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

/* Win32 VK -> matrix cell (device_code = col*16+row), inverted from
   keybddr.dll's (col,row)->VK table dword_14B1058 (sub_14B195C indexes by
   rawcode, sub_14B2C0C default case: VK == table byte). Host modifiers arrive
   generic (HostInputCapture::NormalizeVk); OEM Fn cells 0xC1-0xC7 lack a host key. */
constexpr std::array<uint8_t, 256> MakeVkToCell() {
    std::array<uint8_t, 256> m{};
    for (auto& e : m) e = 0xFFu;
    auto set = [&m](uint8_t vk, uint8_t code) { m[vk] = code; };

    set(0x41,  94); set(0x42,  61); set(0x43,  44); set(0x44,  46); set(0x45,  92);
    set(0x46,  28); set(0x47,  13); set(0x48,  29); set(0x49,  96); set(0x4A,  62);
    set(0x4B,   0); set(0x4C,  51); set(0x4D, 110); set(0x4E, 125); set(0x4F,   3);
    set(0x50,  69); set(0x51,  89); set(0x52, 108); set(0x53,  30); set(0x54,  45);
    set(0x55,  14); set(0x56,  12); set(0x57, 105); set(0x58,  16); set(0x59, 109);
    set(0x5A,  32);
    set(0x30, 100); set(0x31,  72); set(0x32,  74); set(0x33,  76); set(0x34,  60);
    set(0x35,  77); set(0x36,  93); set(0x37,  78); set(0x38,  64); set(0x39,  67);
    set(0x0D, 119); set(0x20, 126); set(0x08, 118); set(0x09,  56); set(0x1B,  41);
    set(0x25,  19); set(0x26,  23); set(0x27,   7); set(0x28,   6);
    set(0xBA,  52); set(0xBB,  99); set(0xBC,  80); set(0xBD, 116); set(0xBE,  70);
    set(0xBF,  84); set(0xC0,  24); set(0xDB,  68); set(0xDC, 102); set(0xDD,  83);
    set(0xDE,   4); set(0xDF,  71);
    set(0x5B,  31);
    set(0x10,  59); set(0xA0,  59); set(0xA1,  75);
    set(0x11,  34); set(0xA2,  34);
    set(0x12,  17); set(0xA4,  17);
    return m;
}
constexpr std::array<uint8_t, 256> kVkToCell = MakeVkToCell();

class SharpMobilonHcKeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    void OnReady() override {
        for (int vk = 0; vk < 256; ++vk) {
            const uint8_t code = kVkToCell[vk];
            if (code != 0xFFu)
                bindings_.push_back({ static_cast<uint8_t>(vk), code, nullptr, 0, 0 });
        }
    }

    const std::vector<KeyBinding>& Bindings() const override { return bindings_; }

private:
    std::vector<KeyBinding> bindings_;
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHcKeyboardMap, KeyboardMap);

}
