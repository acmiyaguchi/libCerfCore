#include "pr31x00_uart_transmitter.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include "pr31x00_uart_dma.h"

#include <utility>
#include <vector>

namespace cerfcore {

bool Pr31x00UartTransmitter::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00UartTransmitter::OnReady() {
    dma_ = &emu_.Get<Pr31x00UartDma>();
}

uint32_t Pr31x00UartTransmitter::Attach(uint32_t dma_port, DeliverFn deliver, TxIntFn raise_ints) {
    if (count_ >= kPorts) emu_.Get<Fatal>().Die("Pr31x00UartTransmitter: a third UART attached");
    const uint32_t port = count_;
    ch_[port] = std::make_unique<Channel>();
    ch_[port]->dma_port   = dma_port;
    ch_[port]->deliver    = std::move(deliver);
    ch_[port]->raise_ints = std::move(raise_ints);
    ++count_;
    return port;
}

Pr31x00UartTransmitter::Channel& Pr31x00UartTransmitter::Ch(uint32_t port) { return *ch_[port]; }
const Pr31x00UartTransmitter::Channel& Pr31x00UartTransmitter::Ch(uint32_t port) const {
    return *ch_[port];
}

/* TMPR3911 §6.3.1 (page 6-8): clearing ENUARTACLK / ENUARTBCLK "disables the UARTACLK
   generation, halting the clock to the UARTA Module". */
void Pr31x00UartTransmitter::WriteHold(uint32_t port, uint8_t byte) {
    Channel& c = Ch(port);
    {
        std::lock_guard<std::mutex> lk(c.mu);
        if (!c.clock_running) {
            if (c.hold_full) {
                emu_.Get<Fatal>().Die("Pr31x00UartTransmitter: TXHOLD 0x%02X written while full "
                                      "with the UART clock stopped", byte);
            }
            c.hold      = byte;
            c.hold_full = true;
            return;
        }
    }
    c.deliver(&byte, 1);
    TxInts ints;
    ints.sent = true;
    c.raise_ints(ints);
}

void Pr31x00UartTransmitter::StartDma(uint32_t port) {
    Channel& c = Ch(port);
    {
        std::lock_guard<std::mutex> lk(c.mu);
        if (!c.clock_running) {
            c.dma_pending = true;
            return;
        }
    }
    RunDma(c);
}

void Pr31x00UartTransmitter::CancelDma(uint32_t port) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    c.dma_pending = false;
}

/* TMPR3911 Figure 16.2.1 (printed p.16-6): the one-time transfer takes ADDR CNT through DMA LENGTH / 2
   and DMA LENGTH, the buffer length minus one (§16.2.4), for any length of one byte or more. */
void Pr31x00UartTransmitter::RunDma(Channel& c) {
    std::vector<uint8_t> block;
    dma_->TransmitBlock(c.dma_port, block);
    c.deliver(block.data(), block.size());
    TxInts ints;
    ints.sent     = true;
    ints.dma_full = true;
    ints.dma_half = true;
    c.raise_ints(ints);
}

void Pr31x00UartTransmitter::SetClockRunning(uint32_t port, bool running) {
    Channel& c         = Ch(port);
    bool     send_hold = false;
    bool     run_dma   = false;
    uint8_t  byte      = 0;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        c.clock_running = running;
        if (!running) return;
        send_hold     = c.hold_full;
        byte          = c.hold;
        c.hold_full   = false;
        run_dma       = c.dma_pending;
        c.dma_pending = false;
    }
    if (send_hold) {
        c.deliver(&byte, 1);
        TxInts ints;
        ints.sent = true;
        c.raise_ints(ints);
    }
    if (run_dma) RunDma(c);
}

void Pr31x00UartTransmitter::ResetLine(uint32_t port) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    c.hold_full   = false;
    c.dma_pending = false;
}

bool Pr31x00UartTransmitter::Empty(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return !c.hold_full && !c.dma_pending;
}

void Pr31x00UartTransmitter::SaveState(uint32_t port, StateWriter& w) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    w.Write("tx_hold", c.hold);
    w.Write<uint8_t>("tx_hold_full", c.hold_full ? 1u : 0u);
    w.Write<uint8_t>("tx_dma_pending", c.dma_pending ? 1u : 0u);
}

void Pr31x00UartTransmitter::RestoreState(uint32_t port, StateReader& r) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    uint8_t flag = 0;
    r.Read("tx_hold", c.hold);
    r.Read("tx_hold_full", flag);
    c.hold_full = flag != 0u;
    r.Read("tx_dma_pending", flag);
    c.dma_pending = flag != 0u;
}

REGISTER_SERVICE(Pr31x00UartTransmitter);

}
