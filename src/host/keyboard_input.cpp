#include <cerfcore/host/keyboard_input.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>

bool KeyboardInput::CanDeliverVk(uint8_t vk) const {
    auto* map = emu_.TryGet<KeyboardMap>();
    uint32_t code;
    return map && map->BaseDeviceCode(vk, code);
}
