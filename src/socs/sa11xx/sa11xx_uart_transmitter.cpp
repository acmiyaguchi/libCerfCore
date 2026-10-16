#define NOMINMAX

#include "sa11xx_uart_transmitter.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/state/state_stream.h>
#include "sa11xx_dma.h"
#include "sa11xx_uart_base.h"
#include "sa11xx_uart_receiver.h"
#include "sa11xx_uart_regs.h"

namespace cerfcore {

using namespace sa11xx_uart;

bool Sa11xxUartTransmitter::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxUartTransmitter::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    dma_   = &emu_.Get<Sa11xxDma>();
    rx_    = &emu_.Get<Sa11xxUartReceiver>();
}

uint32_t Sa11xxUartTransmitter::Attach(Sa11xxUartBase* owner, uint32_t utdr,
                                       uint32_t device_select, uint32_t rx_port,
                                       Sa11xxDmaTransmitFifo& fifo) {
    if (count_ >= kPorts) {
        emu_.Get<Fatal>().Die("Sa11xxUartTransmitter: a fourth UART attached");
    }
    const uint32_t index = count_++;
    ch_[index] = std::make_unique<Channel>(*this, index);
    Channel& c = *ch_[index];
    c.owner = owner;
    c.utdr    = utdr;
    c.rx_port = rx_port;
    c.fifo    = &fifo;
    fifo.Attach(&c.port);
    dma_->RegisterPort(device_select, &c.port);
    return index;
}

uint32_t Sa11xxUartTransmitter::Port::DeviceAddress() const {
    return sa11xx_uart::DeviceAddress(t_.ch_[index_]->utdr);
}

GuestCycleClock::Rate Sa11xxUartTransmitter::FrameRate(const Channel& c) {
    const GuestCycleClock::Rate bits =
        c.rate.num != 0u ? c.rate : sa11xx_uart::BitRate(c.ctl.utcr1, c.ctl.utcr2);
    return bits.Scaled(1u, sa11xx_uart::FrameBits(c.ctl.utcr0));
}

/* §11.11.5.6 (printed 11-117): "When LBM=1, the output of the transmit serial shifter is directly
   connected to the input of the receive serial shifter internally". */
bool Sa11xxUartTransmitter::LoopLow(const Control& ctl) {
    constexpr uint32_t kLow = kUtcr3Txe | kUtcr3Brk | kUtcr3Lbm;
    return (ctl.utcr3 & kLow) == kLow;
}

/* §11.11.5.3 (printed 11-116): "Asserting BRK also prevents the transmit logic from fetching any
   additional data from the transmit FIFO". */
void Sa11xxUartTransmitter::Drain(Channel& c, Bytes& out) {
    SendLoopBreak(c);
    if (c.clocked && (c.ctl.utcr3 & kUtcr3Txe) != 0u && (c.ctl.utcr3 & kUtcr3Brk) == 0u) {
        const bool    looped = (c.ctl.utcr3 & kUtcr3Lbm) != 0u;
        const uint8_t mask   = static_cast<uint8_t>((1u << DataBits(c.ctl.utcr0)) - 1u);
        c.taken.clear();
        while (c.fifo->Take(1u, c.taken) == 0u) {
            const uint8_t byte = static_cast<uint8_t>(c.taken.back() & mask);
            if (looped) {
                rx_->Loop(c.rx_port, byte);
            } else {
                out.push_back(byte);
            }
        }
    }
    Publish(c);
}

/* §11.11.7.4 (printed 11-121): the RBB interlock "is cleared when REB is set, when RXE is cleared,
   or when the SA-1110 is reset". */
void Sa11xxUartTransmitter::SendLoopBreak(Channel& c) {
    if ((c.ctl.utcr3 & kUtcr3Rxe) == 0u) c.break_sent = false;
    if (c.low && !c.break_sent) c.break_sent = rx_->LoopBreak(c.rx_port, true);
}

/* §11.11.7.5 (printed 11-121): REB "is set when the receive pin transitions high (rising edge) and
   the RBB interlock is currently set". */
void Sa11xxUartTransmitter::EndLoopLow(Channel& c) {
    if (c.break_sent && (c.ctl.utcr3 & kUtcr3Rxe) != 0u) rx_->LoopBreak(c.rx_port, false);
    c.low        = false;
    c.break_sent = false;
}

void Sa11xxUartTransmitter::Clock(Channel& c) {
    GuestCycleClock::Rate rate{};
    c.clocked = c.owner->BitClock(c.ctl, rate);
    if (c.clocked) c.rate = rate;
}

/* §11.11.5.2: TXE cleared while transmitting: "all entries within the transmit FIFO are reset". */
void Sa11xxUartTransmitter::StopTransmit(Channel& c) {
    c.clocked = false;
    c.fifo->Clear();
    Publish(c);
}

/* §11.11.7.1: TFS "1 - Transmit FIFO is half-full (four or fewer entries filled) and transmitter
   operation is enabled". */
void Sa11xxUartTransmitter::Publish(Channel& c) {
    c.tfs.store((c.ctl.utcr3 & kUtcr3Txe) != 0u && c.fifo->Level() <= kTxRequest,
                std::memory_order_release);
}

/* §11.11.8.3: TNF "0 - Transmit FIFO is full". */
bool Sa11xxUartTransmitter::Tnf(uint32_t port) const {
    return ch_[port]->fifo->Level() < kTxDepth;
}

bool Sa11xxUartTransmitter::WriteControl(uint32_t port, const Control& ctl, Bytes& out) {
    Channel& c = *ch_[port];
    Drain(c, out);
    const bool was     = (c.ctl.utcr3 & kUtcr3Txe) != 0u;
    const bool txe     = (ctl.utcr3 & kUtcr3Txe) != 0u;
    const bool low_was = LoopLow(c.ctl);
    c.ctl = ctl;
    if (was && !txe) StopTransmit(c);
    if (txe) Clock(c);
    const bool low = LoopLow(ctl);
    if (low && !low_was) {
        c.low        = true;
        c.break_sent = false;
    } else if (!low && low_was) {
        EndLoopLow(c);
    }
    Drain(c, out);
    return was != txe;
}

void Sa11xxUartTransmitter::ClockChanged(uint32_t port, Bytes& out) {
    Channel& c = *ch_[port];
    if ((c.ctl.utcr3 & kUtcr3Txe) == 0u) return;
    Drain(c, out);
    Clock(c);
    Drain(c, out);
}

void Sa11xxUartTransmitter::Write(uint32_t port, uint8_t value, Bytes& out) {
    Channel& c = *ch_[port];
    Drain(c, out);
    if (!c.fifo->Put(value)) return;
    Drain(c, out);
}

void Sa11xxUartTransmitter::Settle(uint32_t port, Bytes& out) {
    Drain(*ch_[port], out);
}

/* §11.11.1 (printed 11-109): "Reset also causes the UART's transmit and receive FIFOs to be
   flushed"; UTCR3 reset row: TXE 0. */
void Sa11xxUartTransmitter::Reset(uint32_t port) {
    Channel& c = *ch_[port];
    c.ctl.utcr3 &= ~(kUtcr3Rxe | kUtcr3Txe);
    StopTransmit(c);
    c.low        = false;
    c.break_sent = false;
}

void Sa11xxUartTransmitter::NotifyDma() {
    dma_->OnPortChange();
}

void Sa11xxUartTransmitter::PortSettle(uint32_t port) {
    Bytes out;
    Settle(port, out);
    ch_[port]->owner->OnTransmitted(out);
}

/* §11.11.7.1: "When the TFS bit is set, a DMA service request is made". */
void Sa11xxUartTransmitter::SetDmaSupply(uint32_t port, uint64_t words) {
    Channel& c = *ch_[port];
    Bytes out;
    Drain(c, out);
    c.fifo->SetSupply((c.ctl.utcr3 & kUtcr3Txe) != 0u ? words : 0u);
    Drain(c, out);
    c.owner->OnTransmitted(out);
}

bool Sa11xxUartTransmitter::CycleOfMoved(uint32_t port, uint64_t words, uint64_t& cycle) {
    if (words > DmaMoved(port)) return false;
    cycle = clock_->Cycles();
    return true;
}

void Sa11xxUartTransmitter::Save(uint32_t port, StateWriter& w) {
    const Channel& c = *ch_[port];
    w.Write<uint32_t>("tx_utcr0", c.ctl.utcr0);
    w.Write<uint32_t>("tx_utcr1", c.ctl.utcr1);
    w.Write<uint32_t>("tx_utcr2", c.ctl.utcr2);
    w.Write<uint32_t>("tx_utcr3", c.ctl.utcr3);
    w.Write<uint8_t>("tx_clocked", c.clocked ? 1u : 0u);
    w.Write<uint64_t>("tx_bit_rate_num", c.rate.num);
    w.Write<uint64_t>("tx_bit_rate_den", c.rate.den);
    c.fifo->Save(w);
    w.Write<uint8_t>("tx_loop_low", c.low ? 1u : 0u);
    w.Write<uint8_t>("tx_loop_break_sent", c.break_sent ? 1u : 0u);
}

void Sa11xxUartTransmitter::Restore(uint32_t port, StateReader& r) {
    Channel& c = *ch_[port];
    uint8_t  clocked = 0;
    r.Read("tx_utcr0", c.ctl.utcr0);
    r.Read("tx_utcr1", c.ctl.utcr1);
    r.Read("tx_utcr2", c.ctl.utcr2);
    r.Read("tx_utcr3", c.ctl.utcr3);
    r.Read("tx_clocked", clocked);
    r.Read("tx_bit_rate_num", c.rate.num);
    r.Read("tx_bit_rate_den", c.rate.den);
    c.fifo->Restore(r);
    c.clocked = clocked != 0u;
    uint8_t low = 0, sent = 0;
    r.Read("tx_loop_low", low);
    r.Read("tx_loop_break_sent", sent);
    c.low        = low != 0u;
    c.break_sent = sent != 0u;
    Publish(c);
}

REGISTER_SERVICE(Sa11xxUartTransmitter);

}
