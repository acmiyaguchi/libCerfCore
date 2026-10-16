#pragma once

#include <cerfcore/host/keyboard_map.h>

#include <cstdint>
#include <vector>

/* Shared Win32 VK -> PS/2 Set 2 scancode map. device_code = scancode | extended<<8. */

struct Ps2Set2Entry {
    uint8_t scancode;
    bool    extended;
};

/* VK (0..255) -> Set 2 scancode; scancode==0 means the VK has no mapping. */
const Ps2Set2Entry* Ps2Set2Table();

/* Base-layer KeyBinding list for a PS/2 Set 2 keyboard, built from the table
   above (device_code = scancode | extended<<8). Consumed by a board's
   KeyboardMap::OnReady so both the input path and the mapping dialog share it. */
std::vector<KeyBinding> Ps2Set2KeyBindings();
