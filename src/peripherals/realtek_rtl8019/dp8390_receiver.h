#pragma once

#include "dp8390.h"

#include <cstddef>
#include <cstdint>

/* DP8390D datasheet, 1992 National LAN databook, address recognition
   p. 1-133 and buffer ring management p. 1-138. */
class Dp8390Receiver {
public:
    Dp8390Receiver(Dp8390& nic, std::array<uint8_t, Dp8390::kRamSize>& ram);

    bool OnFrameLocked(const uint8_t* frame, std::size_t len);

private:
    /* DP8390D datasheet, 1992 National LAN databook, destination address
       p. 1-133, RCR p. 1-155, MAR0-MAR7 p. 1-158. */
    bool AcceptsDestinationLocked(const uint8_t* dest) const;

    Dp8390& nic_;
    std::array<uint8_t, Dp8390::kRamSize>& ram_;
};
