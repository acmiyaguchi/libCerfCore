#include "pr31x00_uart_dma.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/host_request_channel.h>
#include <cerfcore/state/state_stream.h>
#include "pr31500_id.h"
#include "pr31700_id.h"

#include <utility>

namespace {

constexpr size_t kWireMax = 64u * 1024u;

/* ADDR CNT, 16 bits (TMPR3911 Figure 16.2.1, printed p.16-6). */
constexpr uint32_t kAddrCntMask = 0x0000FFFFu;

}

bool Pr31x00UartDma::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00UartDma::OnReady() {
    mem_           = &emu_.Get<EmulatedMemory>();
    host_requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    host_requests_->RegisterListener([this] {
        for (uint32_t i = 0; i < count_; ++i) LandPending(*ch_[i]);
    });
}

uint32_t Pr31x00UartDma::Attach(const char* source, RxIntFn raise_ints, LineIdleFn on_line_idle) {
    if (count_ >= kPorts) emu_.Get<Fatal>().Die("Pr31x00UartDma: a third UART attached");
    const uint32_t port = count_;
    ch_[port] = std::make_unique<Channel>();
    ch_[port]->source       = source;
    ch_[port]->raise_ints   = std::move(raise_ints);
    ch_[port]->on_line_idle = std::move(on_line_idle);
    ++count_;
    return port;
}

Pr31x00UartDma::Channel& Pr31x00UartDma::Ch(uint32_t port) { return *ch_[port]; }
const Pr31x00UartDma::Channel& Pr31x00UartDma::Ch(uint32_t port) const { return *ch_[port]; }

void Pr31x00UartDma::SetBuffer(uint32_t port, uint32_t pa) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    c.buffer_pa = pa;
}

void Pr31x00UartDma::SetLength(uint32_t port, uint32_t bytes) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    c.length = bytes;
}

/* TMPR3911 §16.2.4 (page 16-5): "For the receiver, this means that when the Receive Holding
   Register becomes full, the DMA controller will transfer a byte from the Receive Holding
   Register to the specified buffer". */
void Pr31x00UartDma::SetControl(uint32_t port, bool receiver_on, bool armed) {
    Channel& c = Ch(port);
    RxInts   ints;
    bool     dropped = false;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        c.receiver_on = receiver_on;
        /* TMPR3911 §16.5.1 (page 16-13): with ENUART cleared "the module is kept in a reset
           state"; sharp_mobilon_hc4100_hpc2 serial.dll sub_1474498 reopens with ENDMARX still set
           and its read cursor at the buffer base, and sub_1474634 reads to base + DMACNT. */
        if (!receiver_on) {
            dropped        = c.wire_pos < c.wire.size();
            c.wire.clear();
            c.wire_pos     = 0;
            c.count        = 0;
            c.rxhold_full  = false;
            c.prxhold_full = false;
            c.rxshift_full = false;
        }
        if (armed != c.armed) {
            c.armed = armed;
            if (armed) {
                c.count = 0;
                DrainHoldsLocked(c, ints);
            }
        }
    }
    Notify(c, ints, dropped);
}

/* TMPR3911 §16.5.1 (page 16-11): PRXHOLDFULL and RXHOLDFULL RESET 0. */
void Pr31x00UartDma::ResetLine(uint32_t port) {
    Channel& c = Ch(port);
    bool     dropped;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        c.receiver_on  = false;
        c.armed        = false;
        c.rxhold_full  = false;
        c.prxhold_full = false;
        c.rxshift_full = false;
        dropped        = c.wire_pos < c.wire.size();
        c.wire.clear();
        c.wire_pos = 0;
    }
    Notify(c, RxInts{}, dropped);
}

void Pr31x00UartDma::SetClockRunning(uint32_t port, bool running) {
    Channel& c = Ch(port);
    {
        std::lock_guard<std::mutex> lk(c.mu);
        c.clock_running = running;
    }
    LandPending(c);
}

uint32_t Pr31x00UartDma::Count(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return c.count;
}

uint32_t Pr31x00UartDma::Length(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return c.length;
}

bool Pr31x00UartDma::LineIdle(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return c.wire_pos >= c.wire.size();
}

bool Pr31x00UartDma::RxHoldFull(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return c.rxhold_full;
}

bool Pr31x00UartDma::PrxHoldFull(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return c.prxhold_full;
}

void Pr31x00UartDma::Receive(uint32_t port, const uint8_t* data, size_t n) {
    Channel& c = Ch(port);
    {
        std::lock_guard<std::mutex> lk(c.mu);
        if (!c.receiver_on) return;

        if (c.wire.size() - c.wire_pos + n > kWireMax) {
            LOG(Caution, "[UART] %s RX: %zu host bytes dropped, %zu bytes already wait to land "
                         "(cap %zu)\n", c.source, n, c.wire.size() - c.wire_pos, kWireMax);
            return;
        }
        if (c.wire_pos >= c.wire.size()) {
            c.wire.clear();
            c.wire_pos = 0;
        }
        c.wire.insert(c.wire.end(), data, data + n);
    }
    host_requests_->Request();
}

/* TMPR3911 §16.2.4 (pages 16-5 / 16-6): "For the transmitter, this means that when the
   Transmit Holding Register becomes empty, the DMA controller will transfer a byte from the
   specified buffer"; with ENDMALOOP clear "DMA will stop when the end of the buffer is reached". */
void Pr31x00UartDma::TransmitBlock(uint32_t port, std::vector<uint8_t>& out) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    out.resize(c.length);
    for (uint32_t i = 0; i < c.length; ++i) out[i] = mem_->ReadByte(c.buffer_pa + i);
    c.count = c.length - 1u;
}

/* TMPR3911 §6.3.1 (printed p.6-8): clearing ENUARTACLK "disables the UARTACLK generation,
   halting the clock to the UARTA Module". */
void Pr31x00UartDma::LandPending(Channel& c) {
    RxInts ints;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        if (c.wire_pos >= c.wire.size()) return;
        if (c.clock_running) {
            while (c.wire_pos < c.wire.size()) ReceiveByteLocked(c, c.wire[c.wire_pos++], ints);
        }
        c.wire.clear();
        c.wire_pos = 0;
    }
    Notify(c, ints, true);
}

void Pr31x00UartDma::ReceiveByteLocked(Channel& c, uint8_t byte, RxInts& ints) {
    if (c.armed) {
        StoreLocked(c, byte, ints);
    } else if (!c.rxhold_full) {
        c.rxhold      = byte;
        c.rxhold_full = true;
        ints.rx       = true;
    } else if (!c.prxhold_full) {
        c.prxhold      = byte;
        c.prxhold_full = true;
    } else {
        c.rxshift      = byte;
        c.rxshift_full = true;
        ints.overrun   = true;
    }
}

/* TMPR3911 Figure 16.2.1 (printed p.16-6): ADDR CNT against DMA LENGTH, the buffer length minus
   one (§16.2.4), gives the end-of-buffer interrupt, and against DMA LENGTH / 2 the half-buffer
   interrupt. */
void Pr31x00UartDma::StoreLocked(Channel& c, uint8_t byte, RxInts& ints) {
    if (c.length == 0u) {
        emu_.Get<Fatal>().Die("PR31x00 UART %s RX DMA armed with no DMALENGTH programmed",
                              c.source);
    }
    const uint32_t at  = c.count;
    const uint32_t end = c.length - 1u;
    mem_->WriteByte(c.buffer_pa + at, byte);
    if (at == end / 2u) ints.dma_half = true;
    if (at == end) {
        ints.dma_full = true;
        c.count       = 0;
    } else {
        c.count = (at + 1u) & kAddrCntMask;
    }
    ints.rx = true;
}

void Pr31x00UartDma::DrainHoldsLocked(Channel& c, RxInts& ints) {
    if (c.rxhold_full) {
        StoreLocked(c, c.rxhold, ints);
        c.rxhold_full = false;
    }
    if (c.prxhold_full) {
        StoreLocked(c, c.prxhold, ints);
        c.prxhold_full = false;
    }
    if (c.rxshift_full) {
        StoreLocked(c, c.rxshift, ints);
        c.rxshift_full = false;
    }
}

void Pr31x00UartDma::Notify(Channel& c, const RxInts& ints, bool line_idle) {
    if (ints.rx || ints.overrun || ints.dma_half || ints.dma_full) c.raise_ints(ints);
    if (line_idle) c.on_line_idle();
}

void Pr31x00UartDma::SaveState(uint32_t port, StateWriter& w) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    w.Write("buffer_pa", c.buffer_pa);
    w.Write("length", c.length);
    w.Write("count", c.count);
    w.Write<uint8_t>("armed", c.armed ? 1u : 0u);
    w.Write<uint8_t>("receiver_on", c.receiver_on ? 1u : 0u);
    w.Write("rxhold", c.rxhold);
    w.Write<uint8_t>("rxhold_full", c.rxhold_full ? 1u : 0u);
    w.Write("prxhold", c.prxhold);
    w.Write<uint8_t>("prxhold_full", c.prxhold_full ? 1u : 0u);
    w.Write("rxshift", c.rxshift);
    w.Write<uint8_t>("rxshift_full", c.rxshift_full ? 1u : 0u);
}

void Pr31x00UartDma::RestoreState(uint32_t port, StateReader& r) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    r.Read("buffer_pa", c.buffer_pa);
    r.Read("length", c.length);
    r.Read("count", c.count);
    uint8_t flag = 0;
    r.Read("armed", flag);
    c.armed = flag != 0u;
    r.Read("receiver_on", flag);
    c.receiver_on = flag != 0u;
    r.Read("rxhold", c.rxhold);
    r.Read("rxhold_full", flag);
    c.rxhold_full = flag != 0u;
    r.Read("prxhold", c.prxhold);
    r.Read("prxhold_full", flag);
    c.prxhold_full = flag != 0u;
    r.Read("rxshift", c.rxshift);
    r.Read("rxshift_full", flag);
    c.rxshift_full = flag != 0u;

    c.wire.clear();
    c.wire_pos = 0;
}

REGISTER_SERVICE(Pr31x00UartDma);
