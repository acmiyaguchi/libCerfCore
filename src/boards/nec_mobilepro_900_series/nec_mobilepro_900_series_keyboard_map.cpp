#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

/* Win32 VK -> NEC P530 key-matrix bit (0..103); device_code IS the bit. Bit
   index is the key's row in keybddr.dll's reader table dword_1BD1608 (column-0
   keycode is the VK for regular keys). */
constexpr std::array<uint8_t, 256> MakeVkToBit() {
    std::array<uint8_t, 256> m{};
    for (auto& e : m) e = 0xFFu;
    auto set = [&m](uint8_t vk, uint8_t bit) { m[vk] = bit; };

    set(0x41, 64); set(0x42, 52); set(0x43, 74); set(0x44, 66); set(0x45, 58);
    set(0x46, 67); set(0x47, 44); set(0x48, 45); set(0x49, 39); set(0x4A, 46);
    set(0x4B, 47); set(0x4C, 88); set(0x4D, 54); set(0x4E, 53); set(0x4F, 80);
    set(0x50, 26); set(0x51, 56); set(0x52, 59); set(0x53, 65); set(0x54, 36);
    set(0x55, 38); set(0x56, 75); set(0x57, 57); set(0x58, 73); set(0x59, 37);
    set(0x5A, 72);
    set(0x30, 49); set(0x31, 40); set(0x32, 41); set(0x33, 42); set(0x34, 43);
    set(0x35, 60); set(0x36, 61); set(0x37, 62); set(0x38, 63); set(0x39, 48);
    set(0x0D, 89); set(0x09, 25); set(0x08, 27); set(0x20, 95); set(0x1B, 32);
    set(0x2E, 33); set(0x14, 34);
    set(0x26, 83); set(0x28, 82); set(0x25, 91); set(0x27, 90);
    set(0xBA, 84); set(0xBB, 70); set(0xBC, 55); set(0xBD, 69); set(0xBE, 81);
    set(0xBF, 92); set(0xC0, 68); set(0xDB, 86); set(0xDC, 93); set(0xDD, 94);
    set(0xDE, 85);
    set(0x10, 4);  set(0xA0, 4);  set(0xA1, 4);   /* Shift. */
    set(0x11, 13); set(0xA2, 13); set(0xA3, 13);  /* Ctrl. */
    set(0x12, 22); set(0xA4, 22); set(0xA5, 31);  /* Alt (L=22, R=31). */
    return m;
}
constexpr std::array<uint8_t, 256> kVkToBit = MakeVkToBit();

class NecMobilepro900SeriesKeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
    }

    void OnReady() override {
        for (int vk = 0; vk < 256; ++vk) {
            const uint8_t bit = kVkToBit[vk];
            if (bit != 0xFFu)
                bindings_.push_back({ static_cast<uint8_t>(vk), bit, nullptr, 0, 0 });
        }
    }

    const std::vector<KeyBinding>& Bindings() const override { return bindings_; }

private:
    std::vector<KeyBinding> bindings_;
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilepro900SeriesKeyboardMap, KeyboardMap);

}
