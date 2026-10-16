#pragma once

#include "../core/service.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

/* Single source of truth for one board's host-key -> guest mapping, read by both
   the board's KeyboardInput (input path) and KeyboardMappingDialog (the replica).
   device_code is opaque: each KeyboardMap concrete and its matching KeyboardInput
   privately agree on the encoding, documented next to that board's bindings. */
struct KeyBinding {
    uint8_t        vk;
    uint32_t       device_code;  /* Opaque, board-interpreted output. */
    const char*    guest_label;
    uint8_t        layer;        /* 0 = base layer; N = a binding visible in the
                                    dialog only while the modifier holding layer
                                    N is held. */
    uint8_t        holds_layer;  /* 0 = not a modifier; N = holding this host key
                                    activates layer N in the dialog (e.g. Fn). */
};

/* A lock-style layer (e.g. Num Lock) with no key that holds it: the dialog shows
   a checkbox that toggles its preview. */
struct KeyboardToggleLayer {
    uint8_t     layer;
    const char* name;
};

class KeyboardMap : public Service {
public:
    using Service::Service;
    ~KeyboardMap() override = default;

    /* Every binding: the base layer plus all modifier/lock layers. */
    virtual const std::vector<KeyBinding>& Bindings() const = 0;

    /* Lock layers shown as dialog checkboxes; empty if the board has none. */
    virtual std::vector<KeyboardToggleLayer> ToggleLayers() const { return {}; }

    /* Base-layer (input-path) device_code for a host VK; false if unmapped. */
    bool BaseDeviceCode(uint8_t vk, uint32_t& out) const {
        for (const KeyBinding& b : Bindings())
            if (b.vk == vk && b.layer == 0) { out = b.device_code; return true; }
        return false;
    }
};

}
