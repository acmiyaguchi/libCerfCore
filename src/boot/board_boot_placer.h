#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

/* Optional hook RomPlacer calls at the END of its placement (after the NOR
   0xFF fill + image copy). A board uses it for boot-time guest-RAM writes that
   must land after ROM placement - e.g. a flash copy-source a kernel stub reads
   before the MMU is up. */
class BoardBootPlacer : public Service {
public:
    using Service::Service;
    virtual void PlaceAfterRom() = 0;
};

}
