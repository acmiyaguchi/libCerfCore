#pragma once

#include "freescale_uart_receiver.h"

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class FreescaleUartAutobaud {
public:
    void Reset();
    void Configure(bool adbr, bool adnimp);
    bool Handle(const FreescaleUartReceiver::Events& events, uint32_t ubir, uint32_t ubmr);
    void ClearFlags(uint32_t usr2);

    uint32_t Usr2() const;
    uint16_t Ubrc() const { return ubrc_; }

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    bool Armed() const { return adbr_ && !adet_; }
    void OnStart(uint64_t tick);
    bool OnEdge(uint64_t tick, bool rising, uint32_t ubir, uint32_t ubmr);
    void OnStored(uint16_t entry);

    bool     adbr_      = false;
    bool     adnimp_    = false;
    bool     adet_      = false;
    bool     acst_      = false;
    bool     measuring_ = false;
    uint64_t from_      = 0;
    uint16_t ubrc_      = 4;
};

}
