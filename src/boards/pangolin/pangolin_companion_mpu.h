#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

/* smartbook_g138_ce4_2 kbdmouse.dll sub_1B52CDC: 0xBF / 0xDF five-byte frames go to
   penmount.dll sub_1B43070, 0xFA / 0xF0 two-byte frames to the keyboard (sub_1B526BC). */
class PangolinCompanionMpu : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void SendTouch(bool contact, uint16_t x14, uint16_t y14);
    void SendKey(uint8_t scancode, bool down);
};
