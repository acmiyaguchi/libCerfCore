#include "freescale_uart_escape.h"

#include <cerfcore/state/state_stream.h>
#include "freescale_uart_regs.h"

namespace cerfcore {

using namespace cerf_freescale_uart;

namespace {

/* MCIMX51RM section 59.4.5.7 (page 59-50): "An escape sequence typically consists of 3 characters
   entered in rapid succession (such as +++)". */
constexpr uint32_t kSequenceLength = 3u;

}

void FreescaleUartEscape::Attach() {
    ref_.Attach(1u, 1u);
    ref_.SetCounting(false);
}

/* MCIMX51RM page 59-51: "The internal frequency is obtained after the UART internal divider which
   is applied on module_clock clock"; ONEMS "must contain the value of the UART internal frequency
   divided by 1000". */
void FreescaleUartEscape::SetReference(GuestCycleClock::Rate module, uint32_t divider, bool running) {
    if (!running || module.num == 0u) {
        ref_.SetCounting(false);
        return;
    }
    const GuestCycleClock::Rate ref = module.Scaled(1u, divider).Reduced();
    ref_.SetOscRate(ref.num, ref.den);
    ref_.SetCounting(true);
}

void FreescaleUartEscape::Reset() {
    escf_  = false;
    count_ = 0u;
    last_  = 0u;
}

/* MCIMX51RM section 59.4.5.7 (page 59-50): "When an escape character is detected, the internal
   escape timer starts to count"; "Too much time between two of the "+" characters is interpreted as
   two "+" characters". */
void FreescaleUartEscape::OnStored(uint64_t ref_now, uint16_t entry, bool escen, uint32_t esc_char,
                                   uint64_t timeout_ref_clocks) {
    if (!escen || (entry & 0xFFu) != (esc_char & 0xFFu)) {
        count_ = 0u;
        return;
    }
    if (count_ != 0u && ref_now - last_ > timeout_ref_clocks) count_ = 0u;
    ++count_;
    last_ = ref_now;
    if (count_ < kSequenceLength) return;
    escf_  = true;
    count_ = 0u;
}

void FreescaleUartEscape::Handle(const FreescaleUartReceiver::Events& events, bool escen, uint32_t esc_char,
                                 uint64_t timeout_ref_clocks) {
    const uint64_t now = ref_.Now();
    for (const FreescaleUartRxEvent& e : events) {
        if (e.kind == FreescaleUartRxEvent::Kind::kStored) OnStored(now, e.entry, escen, esc_char, timeout_ref_clocks);
    }
}

void FreescaleUartEscape::ClearFlags(uint32_t usr1) {
    if ((usr1 & kUsr1Escf) != 0u) escf_ = false;
}

uint32_t FreescaleUartEscape::Usr1() const { return escf_ ? kUsr1Escf : 0u; }

void FreescaleUartEscape::Save(StateWriter& w) {
    w.Write<uint8_t>("esc_escf", escf_ ? 1u : 0u);
    w.Write("esc_count", count_);
    w.Write("esc_last_ref", last_);
    ref_.Save(w, "esc_ref_");
}

void FreescaleUartEscape::Restore(StateReader& r) {
    uint8_t escf = 0;
    r.Read("esc_escf", escf);
    r.Read("esc_count", count_);
    r.Read("esc_last_ref", last_);
    ref_.Restore(r, "esc_ref_");
    escf_ = escf != 0u;
}

}
