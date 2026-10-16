#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <optional>
#include <string>

struct Pr31x00SerialPin {
    enum class Bank { Mfio, Io };

    Bank bank = Bank::Mfio;
    int  pin  = -1;   /* -1: the board does not wire this line */

    bool Wired() const { return pin >= 0; }

    static Pr31x00SerialPin OnMfio(int p) { return {Bank::Mfio, p}; }
    static Pr31x00SerialPin OnIo(int p)   { return {Bank::Io, p}; }
};

/* Every line is active low: an asserted line reads its pin LOW, so a wiring that
   drops the inversion leaves the guest seeing carrier and CTS permanently asserted
   (Velo SET_DTR sub_1EB2E30 clears IODOUT<3>, CLR_DTR sub_1EB2DC0 sets it). */
struct Pr31x00SerialModem {
    std::string      label;
    Pr31x00SerialPin cts, dcd, dsr, ri;   /* inputs: an endpoint drives these */
    Pr31x00SerialPin dtr, rts;            /* outputs: the guest drives these */
};

class Pr31x00SerialWiring : public Service {
public:
    using Service::Service;

    virtual std::optional<Pr31x00SerialModem> ForUart(uint32_t mmio_base) const = 0;
};
