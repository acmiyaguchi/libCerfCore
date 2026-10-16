#include "freescale_uart_autobaud.h"

#include <cerfcore/state/state_stream.h>
#include "freescale_uart_regs.h"

namespace cerfcore {

using namespace cerf_freescale_uart;

/* MCIMX51RM Table 59-19 (page 59-30): "The 16 bit Baud Rate Count register is reset to 4". */
void FreescaleUartAutobaud::Reset() {
    adet_ = acst_ = measuring_ = false;
    ubrc_ = 4u;
}

void FreescaleUartAutobaud::Configure(bool adbr, bool adnimp) {
    adbr_   = adbr;
    adnimp_ = adnimp;
    if (!adbr) measuring_ = false;
}

/* MCIMX51RM section 59.4.5.6 (page 59-48): "once the beginning of start bit (transition from 1-to-0
   of RXD) has been detected, UART start a counter (UBRC) working at reference frequency". */
void FreescaleUartAutobaud::OnStart(uint64_t tick) {
    if (!Armed() || measuring_) return;
    measuring_ = true;
    from_      = tick;
}

/* MCIMX51RM section 59.4.5.6.2 (page 59-50): with ADNIMP 1 the count stops at the end of the start
   bit; otherwise "it is stopped at the next falling edge (end of bit0)" and "the result is divided
   by 2"; ACST "is set immediately after the determination of the baud rate". */
bool FreescaleUartAutobaud::OnEdge(uint64_t tick, bool rising, uint32_t ubir, uint32_t ubmr) {
    if (!Armed() || !measuring_ || rising != adnimp_) return false;
    uint64_t count = (tick - from_) * (uint64_t{ubmr} + 1u) / (uint64_t{ubir} + 1u);
    if (!adnimp_) count /= 2u;
    ubrc_      = static_cast<uint16_t>(count > 0xFFFFu ? 0xFFFFu : count);
    acst_      = true;
    measuring_ = false;
    return true;
}

/* MCIMX51RM section 59.4.5.6.1 (page 59-49): "When an ASCII character "A" (0x41) or "a" (0x61) is
   received and no error occurs, the Automatic Detect baud rate bit is set (ADET=1)"; otherwise "the
   auto detection sequence restarts and waits for another 1-to-0 transition". */
void FreescaleUartAutobaud::OnStored(uint16_t entry) {
    if (!Armed()) return;
    measuring_ = false;
    const uint32_t ch = entry & 0xFFu;
    if ((entry & kUrxdErr) == 0u && (ch == 0x41u || ch == 0x61u)) adet_ = true;
}

bool FreescaleUartAutobaud::Handle(const FreescaleUartReceiver::Events& events, uint32_t ubir,
                                   uint32_t ubmr) {
    using Kind = FreescaleUartRxEvent::Kind;
    bool measured = false;
    for (const FreescaleUartRxEvent& e : events) {
        if (e.kind == Kind::kStart)       OnStart(e.tick);
        else if (e.kind == Kind::kStored) OnStored(e.entry);
        else if (OnEdge(e.tick, e.kind == Kind::kRise, ubir, ubmr)) measured = true;
    }
    return measured;
}

void FreescaleUartAutobaud::ClearFlags(uint32_t usr2) {
    if ((usr2 & kUsr2Adet) != 0u) adet_ = false;
    if ((usr2 & kUsr2Acst) != 0u) acst_ = false;
}

uint32_t FreescaleUartAutobaud::Usr2() const {
    return (adet_ ? kUsr2Adet : 0u) | (acst_ ? kUsr2Acst : 0u);
}

void FreescaleUartAutobaud::Save(StateWriter& w) const {
    w.Write<uint8_t>("ab_adbr", adbr_ ? 1u : 0u);
    w.Write<uint8_t>("ab_adnimp", adnimp_ ? 1u : 0u);
    w.Write<uint8_t>("ab_adet", adet_ ? 1u : 0u);
    w.Write<uint8_t>("ab_acst", acst_ ? 1u : 0u);
    w.Write<uint8_t>("ab_measuring", measuring_ ? 1u : 0u);
    w.Write("ab_from", from_);
    w.Write("ab_ubrc", ubrc_);
}

void FreescaleUartAutobaud::Restore(StateReader& r) {
    uint8_t adbr = 0, adnimp = 0, adet = 0, acst = 0, measuring = 0;
    r.Read("ab_adbr", adbr);
    r.Read("ab_adnimp", adnimp);
    r.Read("ab_adet", adet);
    r.Read("ab_acst", acst);
    r.Read("ab_measuring", measuring);
    r.Read("ab_from", from_);
    r.Read("ab_ubrc", ubrc_);
    adbr_      = adbr != 0u;
    adnimp_    = adnimp != 0u;
    adet_      = adet != 0u;
    acst_      = acst != 0u;
    measuring_ = measuring != 0u;
}

}
