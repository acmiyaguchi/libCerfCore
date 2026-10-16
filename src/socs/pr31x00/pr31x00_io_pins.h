#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <optional>

namespace cerfcore {

/* Board wiring behind the PR31x00's 7 general purpose I/O pins (TMPR3911/3912
   ch.9). IODIN reports the logic state of a pin whether or not the chip drives
   it (§9.3.1), so for an input pin that state is a property of the board. */
class Pr31x00IoPins : public Service {
public:
    using Service::Service;

    /* Levels the board drives onto I/O[6:0]. Bits 31-7 are zero. */
    virtual uint32_t IoDin() const = 0;

    /* Levels the board drives onto the 32 multi-function I/O pins (§9.3.4),
       masked by MFIODIN to the pins the chip is not driving. nullopt = the board
       wires no MFIO input, so a read of one halts under FATAL-FIRST. */
    virtual std::optional<uint32_t> MfioDin() const { return std::nullopt; }
};

}
