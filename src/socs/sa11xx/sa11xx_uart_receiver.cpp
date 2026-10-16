#define NOMINMAX

#include "sa11xx_uart_receiver.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include "../../execution/engine_bridge.h"
#include <cerfengine/host_request_channel.h>
#include <cerfcore/state/state_stream.h>
#include "sa11xx_dma.h"
#include "sa11xx_uart_base.h"
#include "sa11xx_uart_regs.h"
#include "sa11xx_uart_rx_line.h"

#include <algorithm>
#include <utility>

namespace cerfcore {

using namespace sa11xx_uart;

namespace {

/* §11.11.7 UTSR0 (printed 11-122): RFS 1, RID 2, RBB 3, REB 4, EIF 5; §11.11.8 UTSR1 (printed
   11-125): RNE 1, PRE 3, FRE 4, ROR 5. */
constexpr uint32_t kUtsr0Rfs = 1u << 1;
constexpr uint32_t kUtsr0Rid = 1u << 2;
constexpr uint32_t kUtsr0Rbb = 1u << 3;
constexpr uint32_t kUtsr0Reb = 1u << 4;
constexpr uint32_t kUtsr0Eif = 1u << 5;
constexpr uint32_t kUtsr1Rne = 1u << 1;
constexpr uint32_t kUtsr1Pre = 1u << 3;
constexpr uint32_t kUtsr1Fre = 1u << 4;
constexpr uint32_t kUtsr1Ror = 1u << 5;

}

bool Sa11xxUartReceiver::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxUartReceiver::OnReady() {
    clock_    = &emu_.Get<GuestCycleClock>();
    requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    dma_      = &emu_.Get<Sa11xxDma>();
    line_     = &emu_.Get<Sa11xxUartRxLine>();
    requests_->RegisterListener([this] { OnHostRequest(); });
    clock_->RegisterRateListener([this] { OnCpuRate(); });
}

uint32_t Sa11xxUartReceiver::Attach(Sa11xxUartBase* owner, uint32_t utdr, uint32_t device_select,
                                    const char* name, Sa11xxDmaReceiveFifo& fifo) {
    if (count_ >= kPorts) emu_.Get<Fatal>().Die("Sa11xxUartReceiver: a fourth UART attached");
    const uint32_t index = count_++;
    ch_[index] = std::make_unique<Channel>(*this, index);
    Channel& c = *ch_[index];
    c.owner = owner;
    c.fifo  = &fifo;
    c.name  = name;
    c.utdr  = utdr;
    c.line  = line_->Attach(owner, name);
    c.ev    = clock_->Add([this, index] { OnEvent(index); });
    fifo.Attach(&c.port);
    dma_->RegisterPort(device_select, &c.port);
    return index;
}

uint32_t Sa11xxUartReceiver::Port::DeviceAddress() const {
    return sa11xx_uart::DeviceAddress(r_.ch_[index_]->utdr);
}

bool Sa11xxUartReceiver::Port::CycleOfMoved(uint64_t words, uint64_t& cycle) {
    return r_.CycleOfMoved(*r_.ch_[index_], words, cycle);
}

GuestCycleClock::Rate Sa11xxUartReceiver::Port::WordRate() const {
    const Channel& c = *r_.ch_[index_];
    GuestCycleClock::Rate bit{};
    if (!c.owner->BitClock(c.ctl, bit)) bit = sa11xx_uart::BitRate(c.ctl.utcr1, c.ctl.utcr2);
    return bit.Scaled(1u, sa11xx_uart::FrameBits(c.ctl.utcr0));
}

/* §11.11.7.2: "The receive FIFO is designed to signal the RFS bit to be set when it contains eight
   entries of valid data"; UTSR0 RFS row: "and receiver operation is enabled". */
bool Sa11xxUartReceiver::Rfs(uint32_t utcr3, const Sa11xxDmaReceiveFifo& fifo) {
    return (utcr3 & kUtcr3Rxe) != 0u && fifo.Filled() >= kRxRequest;
}

void Sa11xxUartReceiver::Store(Channel& c, uint8_t byte, int32_t tags) {
    const int32_t entry =
        static_cast<int32_t>(byte & ((1u << DataBits(c.ctl.utcr0)) - 1u)) | tags;
    if (c.fifo->Store(1u, &entry) != 0u) {
        LOG(SocUart, "[UART] %s RX FIFO full: byte 0x%02X discarded, overrun set\n", c.name, byte);
    }
}

/* §11.11.7.4 (printed 11-121): "When RBB is set, an interrupt is signalled, a single null frame is
   placed in the receive FIFO, the framing error bit is set, and all subsequent null frames with
   framing errors are ignored"; §11.11.7.5: "When REB is set ... the RBB interlock is cleared". */
void Sa11xxUartReceiver::Take(Channel& c, uint16_t entry) {
    if ((entry & Sa11xxUartRxLine::kBreakEnd) != 0u) {
        if (c.break_lock) {
            c.reb        = true;
            c.break_lock = false;
        }
    } else if ((entry & Sa11xxUartRxLine::kBreakBegin) != 0u) {
        if (!c.break_lock) {
            Store(c, 0u, kRxTagFraming);
            c.rbb        = true;
            c.break_lock = true;
        }
    } else {
        Store(c, static_cast<uint8_t>(entry), 0);
    }
}

bool Sa11xxUartReceiver::CycleOfMoved(const Channel& c, uint64_t words, uint64_t& cycle) const {
    if (words <= c.fifo->Moved()) {
        cycle = clock_->Cycles();
        return true;
    }
    uint64_t n = 0;
    if ((c.ctl.utcr3 & kUtcr3Rxe) == 0u || !line_->Runs(c.line, c.ctl) ||
        !c.fifo->TakesToMove(words, n) || n > line_->Pending(c.line)) {
        return false;
    }
    cycle =line_->CycleOfTick(c.line, line_->DueTick(c.line, static_cast<size_t>(n - 1u)));
    return true;
}

void Sa11xxUartReceiver::SettleChannel(Channel& c, uint64_t now) {
    const bool rxe = (c.ctl.utcr3 & kUtcr3Rxe) != 0u;
    if (rxe && line_->FrontDue(c.line, now) && line_->Runs(c.line, c.ctl)) {
        do {
            Take(c, line_->Front(c.line));
            line_->PopFront(c.line, now);
        } while (line_->FrontDue(c.line, now));
    }
    line_->Accept(c.line, c.ctl, now);
    /* §11.11.7.3 (printed 11-121): RID "is set when the receiver is enabled (RXE=1), the receive
       FIFO is not empty ... and three frame periods elapse without any data having being received". */
    if (rxe && !c.rid && c.fifo->Filled() != 0u && line_->Quiet(c.line, c.ctl, now) &&
        line_->Runs(c.line, c.ctl)) {
        c.rid = true;
    }
}

/* §11.11.5.4 (printed 11-117): "When RIE=1 ... whenever RFS or RID is set (one), an interrupt request
   is made"; "RIE does not affect generation of the receive FIFO DMA request that is asserted whenever
   RFS=1". */
void Sa11xxUartReceiver::Arm(Channel& c, uint64_t now) {
    uint64_t   tick = Sa11xxUartRxLine::kNever;
    const bool rie  = (c.ctl.utcr3 & kUtcr3Rie) != 0u;
    if ((c.ctl.utcr3 & kUtcr3Rxe) != 0u) {
        if (rie && c.fifo->Filled() < kRxRequest) {
            const size_t need = kRxRequest - c.fifo->Filled();
            if (line_->Pending(c.line) >= need) tick = line_->DueTick(c.line, need - 1u);
        }
        if (rie && !c.rid && (c.fifo->Filled() != 0u || line_->Pending(c.line) != 0u)) {
            tick = std::min(tick, line_->QuietTick(c.line, c.ctl));
        }
    }
    if (tick == Sa11xxUartRxLine::kNever) {
        clock_->Disarm(c.ev);
    } else {
        clock_->Arm(c.ev, std::max(line_->CycleOfTick(c.line, tick), now));
    }
}

void Sa11xxUartReceiver::OnEvent(uint32_t port) {
    Channel& c = *ch_[port];
    const uint64_t now    = clock_->Cycles();
    const uint64_t before = c.fifo->Moved();
    SettleChannel(c, now);
    Arm(c, now);
    c.owner->OnReceived();
    if (c.fifo->Moved() != before) dma_->OnPortChange();
}

void Sa11xxUartReceiver::OnCpuRate() {
    const uint64_t now      = clock_->Cycles();
    bool           supplied = false;
    for (uint32_t i = 0; i < count_; ++i) {
        Channel& c = *ch_[i];
        SettleChannel(c, now);
        line_->Rescale(c.line, now);
        Arm(c, now);
        supplied = supplied || c.fifo->Supplied();
    }
    if (supplied) dma_->OnPortChange();
}

void Sa11xxUartReceiver::PostRestore(uint32_t port) {
    Arm(*ch_[port], clock_->Cycles());
}

void Sa11xxUartReceiver::Loop(uint32_t port, uint8_t byte) {
    Channel& c = *ch_[port];
    const uint64_t now = clock_->Cycles();
    line_->Loop(c.line, byte, c.ctl, now);
    SettleChannel(c, now);
    Arm(c, now);
}

bool Sa11xxUartReceiver::LoopBreak(uint32_t port, bool low) {
    Channel& c = *ch_[port];
    const uint64_t now   = clock_->Cycles();
    const bool     taken = line_->Break(c.line, low, c.ctl, now);
    SettleChannel(c, now);
    Arm(c, now);
    return taken;
}

void Sa11xxUartReceiver::Push(uint32_t port, const uint8_t* data, size_t n) {
    if (line_->Push(ch_[port]->line, data, n)) requests_->Request();
}

void Sa11xxUartReceiver::PushNow(uint32_t port, const uint8_t* data, size_t n) {
    Channel& c = *ch_[port];
    const uint64_t now = clock_->Cycles();
    line_->Receive(c.line, c.ctl, data, n, now);
    Arm(c, now);
    if (c.fifo->Supplied()) requests_->Request();
}

/* §11.11.5.1 (printed 11-116): "If the RXE bit is cleared to zero while the UART is actively
   receiving data, reception is stopped immediately"; "all entries within the receive FIFO are
   reset (all other control/status/flag bits remain intact)". */
void Sa11xxUartReceiver::StopReceive(Channel& c) {
    line_->Stop(c.line);
    c.fifo->FlushEntries();
    c.break_lock  = false;
}

void Sa11xxUartReceiver::WriteControl(uint32_t port, uint64_t now, const Control& ctl) {
    Channel& c = *ch_[port];
    SettleChannel(c, now);
    const bool    was = (c.ctl.utcr3 & kUtcr3Rxe) != 0u;
    const Control old = c.ctl;
    c.ctl = ctl;
    if (was && (ctl.utcr3 & kUtcr3Rxe) == 0u) {
        StopReceive(c);
        if (c.fifo->Supplied()) c.replan = true;
    }
    if (line_->Retime(c.line, old, ctl, now)) c.replan = true;
    Arm(c, now);
}

void Sa11xxUartReceiver::Settle(uint32_t port, uint64_t now) {
    Channel& c = *ch_[port];
    SettleChannel(c, now);
    Arm(c, now);
}

void Sa11xxUartReceiver::ClockChanged(uint32_t port, uint64_t now) {
    Channel& c = *ch_[port];
    SettleChannel(c, now);
    line_->Retime(c.line, c.ctl, c.ctl, now);
    Arm(c, now);
}

uint8_t Sa11xxUartReceiver::Pop(uint32_t port, uint64_t now) {
    Channel& c = *ch_[port];
    SettleChannel(c, now);
    int32_t       entry  = 0;
    const bool    popped = c.fifo->PopEntry(entry);
    const uint8_t value  = static_cast<uint8_t>(popped ? entry & 0xFF : 0);
    if (popped && c.fifo->Supplied()) c.replan = true;
    Arm(c, now);
    return value;
}

uint32_t Sa11xxUartReceiver::Utsr0(uint32_t port) const {
    const Channel& c = *ch_[port];
    return (Rfs(c.ctl.utcr3, *c.fifo) ? kUtsr0Rfs : 0u) | (c.rid ? kUtsr0Rid : 0u) |
           (c.rbb ? kUtsr0Rbb : 0u) | (c.reb ? kUtsr0Reb : 0u) |
           (c.fifo->RequestBlocked() ? kUtsr0Eif : 0u);
}

/* §11.11.8.6 (printed 11-124): "Each time a data value is transferred to the bottom of the FIFO ...
   the state of this bit is moved from the FIFO to the ROR bit in the status register". */
uint32_t Sa11xxUartReceiver::Utsr1(uint32_t port) const {
    const Channel& c = *ch_[port];
    if (c.fifo->Filled() == 0u) return 0u;
    const int32_t front = c.fifo->FrontEntry();
    return kUtsr1Rne | ((front & kRxTagParity) != 0 ? kUtsr1Pre : 0u) |
           ((front & kRxTagFraming) != 0 ? kUtsr1Fre : 0u) |
           ((front & kRxTagOverrun) != 0 ? kUtsr1Ror : 0u);
}

/* §11.11.7 (printed 11-120): "Writing a one to a sticky status bit clears it". */
void Sa11xxUartReceiver::ClearStatus(uint32_t port, uint64_t now, uint32_t mask) {
    Channel& c = *ch_[port];
    SettleChannel(c, now);
    if ((mask & kUtsr0Rid) != 0u) c.rid = false;
    if ((mask & kUtsr0Rbb) != 0u) c.rbb = false;
    if ((mask & kUtsr0Reb) != 0u) c.reb = false;
    Arm(c, now);
}

/* §11.11.5.4 (printed 11-117): RIE masks "both the receive FIFO service request interrupt and
   receiver idle interrupt"; §11.11.7.4-6: RBB, REB and EIF are nonmaskable interrupts. */
bool Sa11xxUartReceiver::InterruptRequest(uint32_t port) const {
    const Channel& c = *ch_[port];
    return ((c.ctl.utcr3 & kUtcr3Rie) != 0u && (Rfs(c.ctl.utcr3, *c.fifo) || c.rid)) || c.rbb ||
           c.reb || c.fifo->RequestBlocked();
}

void Sa11xxUartReceiver::SetDmaSupply(uint32_t port, uint64_t now, uint64_t words) {
    Channel& c = *ch_[port];
    SettleChannel(c, now);
    c.fifo->SetSupply(words);
    Arm(c, now);
    c.owner->OnReceived();
}

/* §11.11.1 (printed 11-109): "Reset also causes the UART's transmit and receive FIFOs to be
   flushed"; UTCR3 reset row: RXE 0. */
void Sa11xxUartReceiver::Reset(uint32_t port) {
    Channel& c = *ch_[port];
    line_->Reset(c.line);
    c.ctl.utcr3 &= ~(kUtcr3Rxe | kUtcr3Txe);
    StopReceive(c);
    c.rid = false;
    c.rbb = false;
    c.reb = false;
    c.fifo->SetSupply(0u);
    clock_->Disarm(c.ev);
}

void Sa11xxUartReceiver::OnHostRequest() {
    const uint64_t now   = clock_->Cycles();
    bool           moved = false;
    for (uint32_t i = 0; i < count_; ++i) {
        Channel& c = *ch_[i];
        const uint64_t before = c.fifo->Moved();
        SettleChannel(c, now);
        Arm(c, now);
        c.owner->OnReceived();
        moved = moved || c.fifo->Moved() != before || c.fifo->Supplied();
    }
    if (moved) dma_->OnPortChange();
}

void Sa11xxUartReceiver::Save(uint32_t port, StateWriter& w) {
    Channel& c = *ch_[port];
    w.Write<uint32_t>("rx_utcr0", c.ctl.utcr0);
    w.Write<uint32_t>("rx_utcr1", c.ctl.utcr1);
    w.Write<uint32_t>("rx_utcr2", c.ctl.utcr2);
    w.Write<uint32_t>("rx_utcr3", c.ctl.utcr3);
    line_->Save(c.line, w);
    w.Write<uint8_t>("rx_rid", c.rid ? 1u : 0u);
    w.Write<uint8_t>("rx_rbb", c.rbb ? 1u : 0u);
    w.Write<uint8_t>("rx_reb", c.reb ? 1u : 0u);
    w.Write<uint8_t>("rx_break_lock", c.break_lock ? 1u : 0u);
    c.fifo->Save(w);
}

void Sa11xxUartReceiver::Restore(uint32_t port, StateReader& r) {
    Channel& c = *ch_[port];
    uint8_t rid = 0, rbb = 0, reb = 0, lock = 0;
    r.Read("rx_utcr0", c.ctl.utcr0);
    r.Read("rx_utcr1", c.ctl.utcr1);
    r.Read("rx_utcr2", c.ctl.utcr2);
    r.Read("rx_utcr3", c.ctl.utcr3);
    line_->Restore(c.line, r);
    r.Read("rx_rid", rid);
    r.Read("rx_rbb", rbb);
    r.Read("rx_reb", reb);
    r.Read("rx_break_lock", lock);
    c.fifo->Restore(r);
    c.rid        = rid != 0u;
    c.rbb        = rbb != 0u;
    c.reb        = reb != 0u;
    c.break_lock = lock != 0u;
}

REGISTER_SERVICE(Sa11xxUartReceiver);

}
