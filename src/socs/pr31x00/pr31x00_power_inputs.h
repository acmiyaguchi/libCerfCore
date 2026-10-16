#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

/* PR31x00 Power Control Register read-only signal inputs (TMPR3911/3912 §12.3.1).
   PWRINT (bit 30) is a board-wired external signal - on the HC-4100 it is the
   AC-adapter detect that gwes.exe sub_9F38C reads as SYSTEM_POWER_STATUS_EX
   ACLineStatus. A board that wires it provides its level here. */
class Pr31x00PowerInputs : public Service {
public:
    using Service::Service;

    virtual bool PwrIntAsserted() const = 0;
};

}
