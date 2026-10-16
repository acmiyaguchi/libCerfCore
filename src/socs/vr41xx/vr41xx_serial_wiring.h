#pragma once

#include <cerfcore/core/service.h>

#include <optional>
#include <string>

namespace cerfcore {

/* How one board wires the SIU's modem lines. CTS/DSR/RI reach the guest through the
   SIU's own SIUMS, but a board is free to route DCD elsewhere, so the pin it lands on
   is a per-board fact and never a default. */
struct Vr41xxSerialModem {
    std::string  label;

    /* GIU pin carrying DCD, or -1 when DCD is the SIU's own SIUMS bit. Active low. */
    int dcd_giu_pin = -1;
};

class Vr41xxSerialWiring : public Service {
public:
    using Service::Service;

    virtual std::optional<Vr41xxSerialModem> ForSiu() const = 0;
};

}
