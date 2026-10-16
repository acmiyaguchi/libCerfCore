#define NOMINMAX

#include "sa11xx_ssp_stream.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa11xx_dma.h"
#include "sa11xx_gpio.h"
#include "sa11xx_intc.h"
#include "sa11xx_mcp.h"
#include "sa11xx_mcp_regs.h"
#include "sa11xx_ppc.h"
#include "sa11xx_ssp_bit_clock.h"
#include "sa11xx_ssp_device.h"
#include "sa11xx_ssp_receive_fifo.h"
#include "sa11xx_ssp_regs.h"
#include "sa11xx_ssp_transmit_fifo.h"

#include <algorithm>
#include <vector>

namespace cerfcore {

using namespace sa11xx_ssp;

bool Sa11xxSspStream::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxSspStream::OnReady() {
    clock_  = &emu_.Get<GuestCycleClock>();
    intc_   = &emu_.Get<Sa11xxIntc>();
    dma_    = &emu_.Get<Sa11xxDma>();
    gpio_   = &emu_.Get<Sa11xxGpio>();
    mcp_    = &emu_.Get<Sa11xxMcp>();
    ppc_    = &emu_.Get<Sa11xxPpc>();
    bit_    = &emu_.Get<Sa11xxSspBitClock>();
    tx_     = &emu_.Get<Sa11xxSspTransmitFifo>();
    rx_     = &emu_.Get<Sa11xxSspReceiveFifo>();
    device_ = emu_.TryGet<Sa11xxSspDevice>();
    tx_->Attach(&tx_port_);
    rx_->Attach(&rx_port_);
    mcp_->RegisterControlListener([this] { OnPinOwnerChange(); });
    ppc_->RegisterPparListener([this] { OnPinOwnerChange(); });
    gpio_->RegisterPinConfigListener([this] { OnPinsChange(); });
    pins_ = ReadPins();
    line_ev_ = clock_->Add([this] {
        const uint64_t now = clock_->Cycles();
        Settle(now);
        RefreshLine(now);
    });
    dma_->RegisterPort(0xEu, &tx_port_);
    dma_->RegisterPort(0xFu, &rx_port_);
    bit_->SetListener([this](uint64_t now) { BeforeClockChange(now); },
                      [this](uint64_t now) { AfterClockChange(now); });
    /* §11.12.9.4 (printed 11-158): SSE resets to 0; §11.12.9.3: "Clearing SSE resets the SSP's
       FIFOs."; SSSR reset row (printed 11-165): ROR "?"; SSDR read reset row (page 11-163): 0. */
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        Disable();
        bit_->Stop();
        sscr0_ &= ~kSse;
        last_rx_ = 0u;
        rx_->ClearOverrun();
        RefreshLine(clock_->Cycles());
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { pins_ = ReadPins(); });
}

void Sa11xxSspStream::BeforeClockChange(uint64_t now) {
    Settle(now);
}

void Sa11xxSspStream::AfterClockChange(uint64_t now) {
    StartRun(now);
    RefreshLine(now);
    dma_->OnPortChange();
}

/* SA-1110 Developer's Manual §11.13.5.2 (printed 11-172): "When SPR=0, serial port 4 uses its
   TXD4, RXD4, SCLK, and SFRM pins; the MCP enable (MCE) and SSP enable (SSE) bits are used to
   select which protocol is enabled (MCE has precedence over SSE)." */
bool Sa11xxSspStream::OperatingWith(uint32_t sscr0) const {
    if ((sscr0 & kSse) == 0u) return false;
    return (ppc_->Ppar() & Sa11xxPpc::kPparSpr) != 0u || (mcp_->Mccr0() & sa11xx_mcp::kMce) == 0u;
}

/* §11.12.9.3 (printed 11-157): "When the SSP is disabled, all of its clocks are powered down";
   "Clearing SSE resets the SSP's FIFOs." */
void Sa11xxSspStream::ApplyEnable(uint64_t now) {
    const bool on = OperatingWith(sscr0_);
    if (enabled_ && !on) {
        Disable();
        bit_->Stop();
    } else if (!enabled_ && on) {
        enabled_ = true;
        bit_->Start(now, sscr0_, sscr1_);
        StartRun(now);
    }
}

void Sa11xxSspStream::OnPinOwnerChange() {
    const uint64_t now = clock_->Cycles();
    Settle(now);
    pins_ = ReadPins();
    ApplyEnable(now);
    RefreshLine(now);
    dma_->OnPortChange();
}

void Sa11xxSspStream::OnPinsChange() {
    if (!pins_.reassigned) return;
    const PinSnapshot pins = ReadPins();
    if (pins == pins_) return;
    Settle(clock_->Cycles());
    pins_ = pins;
}

Sa11xxSspStream::PinSnapshot Sa11xxSspStream::ReadPins() const {
    PinSnapshot p;
    p.reassigned = (ppc_->Ppar() & Sa11xxPpc::kPparSpr) != 0u;
    for (uint32_t pin = kTxdPin; pin <= kSfrmPin; ++pin) {
        const Sa11xxGpio::PinConfig cfg = gpio_->Pin(pin);
        const uint32_t bit = 1u << pin;
        if (cfg.alternate) p.gafr |= bit;
        if (cfg.output) p.gpdr |= bit;
        if (cfg.latch) p.latch |= bit;
    }
    return p;
}

/* SA-1110 Figure 9-1 (printed 9-2): GPDR enables the pin driver, GAFR selects the alternate
   function output; the alternate function input is the pin. */
bool Sa11xxSspStream::Drives(uint32_t pin) const {
    return (pins_.gafr & pins_.gpdr & (1u << pin)) != 0u;
}

int32_t Sa11xxSspStream::PinWord(uint32_t pin, uint32_t bits) const {
    const uint32_t bit = 1u << pin;
    if ((pins_.gpdr & bit) == 0u || (pins_.gafr & bit) != 0u) return Sa11xxDmaReceiveFifo::kNoValue;
    return (pins_.latch & bit) != 0u ? static_cast<int32_t>((1u << bits) - 1u) : 0;
}

/* §11.13.5.2 (printed 11-172): SPR=0, "serial port 4 uses its TXD4, RXD4, SCLK, and SFRM pins";
   SPR=1, "GPIO 10 for transmit, GPIO 11 for receive, GPIO 12 for serial clock, and GPIO 13 for
   serial frame". */
bool Sa11xxSspStream::DeviceClocked() const {
    if (device_ == nullptr) return false;
    const Sa11xxSspDevice::Pins on = pins_.reassigned ? Sa11xxSspDevice::Pins::Gpio10To13
                                                      : Sa11xxSspDevice::Pins::SerialPort4;
    if (device_->WiredTo() != on) return false;
    return !pins_.reassigned || (Drives(kSclkPin) && Drives(kSfrmPin));
}

/* §11.12.9.3 (page 11-157): "When the SSE bit is cleared during active operation, the SSP is
   disabled immediately, causing the current frame, which is being transmitted, to be terminated". */
void Sa11xxSspStream::Disable() {
    enabled_ = false;
    run_.Reset();
    frames_.clear();
    tx_->Clear();
    rx_->ClearFifo();
}

void Sa11xxSspStream::WriteControl(uint64_t now, uint32_t sscr0, uint32_t sscr1) {
    Settle(now);
    const bool reshape = ((sscr0 ^ sscr0_) & ~kSse) != 0u ||
                         ((sscr1 ^ sscr1_) & (kEcs | kSpo | kSph)) != 0u;
    if (enabled_ && reshape && OperatingWith(sscr0)) {
        run_.End();
        EndRun();
        bit_->Change(now, sscr0, sscr1);
        sscr0_ = sscr0;
        sscr1_ = sscr1;
        fmt_   = Sa11xxSspFrameFormat::Of(sscr0_, sscr1_);
        AfterClockChange(now);
    }
    sscr0_ = sscr0;
    sscr1_ = sscr1;
    fmt_   = Sa11xxSspFrameFormat::Of(sscr0_, sscr1_);
    ApplyEnable(now);
    RefreshLine(now);
}

void Sa11xxSspStream::WriteData(uint64_t now, uint16_t value) {
    Settle(now);
    if (!enabled_) return;
    tx_->Put(value);
    StartRun(now);
    RefreshLine(now);
}

/* §11.12.11 (page 11-162): "When SSDR is read, the bottom entry of receive FIFO is accessed";
   "After SSDR is read, the bottom entry is invalidated". */
uint16_t Sa11xxSspStream::ReadData(uint64_t now) {
    Settle(now);
    int32_t v = Sa11xxDmaReceiveFifo::kNoValue;
    if (!rx_->PopEntry(v)) return last_rx_;
    last_rx_ = v == Sa11xxDmaReceiveFifo::kNoValue ? 0u : static_cast<uint16_t>(v);
    RefreshLine(now);
    return last_rx_;
}

/* §11.12.7.1 Figure 11-33 (page 11-152): "Once the bottom entry of the transmit FIFO contains data,
   SFRM is pulsed high for one SCLK period"; Figure 11-34 (page 11-153): "SFRM is pulled low". */
void Sa11xxSspStream::StartRun(uint64_t now) {
    if (!enabled_ || run_.Running() || tx_->Level() == 0u || !bit_->Clocked()) return;
    const uint64_t h     = bit_->HalfTicksAt(now);
    uint64_t       start = bit_->CycleOfHalfTick(h) == now ? h : h + 1u;
    if (!frames_.empty()) start = std::max(start, frames_.back().push);
    run_.Start(start + (start & 1u));
    if (Instant()) Settle(now);
}

void Sa11xxSspStream::Settle(uint64_t now) {
    if (!enabled_ || !bit_->Clocked()) return;
    uint64_t h = bit_->HalfTicksAt(now);
    if (Instant()) h = std::max(h, DoneHalf());
    SettleLoads(h);
    SettlePushes(h);
}

bool Sa11xxSspStream::Instant() const {
    return device_ != nullptr || Looped();
}

uint64_t Sa11xxSspStream::DoneHalf() const {
    uint64_t end = 0u;
    if (!frames_.empty()) {
        const Frame& last = frames_.back();
        end = last.push != kNever ? last.push : last.load + fmt_.push_last;
    }
    if (!run_.Running()) return end;
    const uint64_t loads = run_.RunLoads() + tx_->TakesBeforeEmpty();
    const uint64_t next  = run_.RunStart() + loads * fmt_.period;
    end = std::max(end, next);
    if (loads != 0u) end = std::max(end, next - fmt_.period + fmt_.push_last);
    return end;
}

void Sa11xxSspStream::SettleLoads(uint64_t half) {
    const uint64_t n = run_.DueLoads(half, fmt_.period);
    if (n == 0u) return;
    std::vector<uint16_t> taken;
    const uint64_t empty = tx_->Take(n, taken);
    uint64_t at = run_.RunStart() + run_.RunLoads() * fmt_.period;
    for (uint16_t v : taken) {
        if (!frames_.empty() && frames_.back().push == kNever) {
            frames_.back().push = frames_.back().load + fmt_.push_cont;
        }
        frames_.push_back(Frame{at, kNever, v});
        at += fmt_.period;
    }
    run_.Loaded(taken.size(), empty != 0u);
    if (empty != 0u) EndRun();
}

void Sa11xxSspStream::EndRun() {
    if (!frames_.empty() && frames_.back().push == kNever) {
        frames_.back().push = frames_.back().load + fmt_.push_last;
    }
}

/* §11.12.10.3 (page 11-160): with LBM=1 "the output of the transmit serial shifter is directly
   connected to the input of the receive serial shifter"; page 11-163: "Received data less than 16
   bits is automatically right justified". */
int32_t Sa11xxSspStream::Received(uint16_t sent) const {
    const int32_t wire = static_cast<int32_t>((sent >> fmt_.tx_shift) & ((1u << fmt_.tx_bits) - 1u));
    if (Looped()) return wire;
    const bool clocked = DeviceClocked();
    int32_t    reply   = Sa11xxDmaReceiveFifo::kNoValue;
    if (clocked) {
        const int32_t in = !pins_.reassigned || Drives(kTxdPin) ? wire : PinWord(kTxdPin, fmt_.tx_bits);
        reply = device_->Exchange(in == Sa11xxDmaReceiveFifo::kNoValue ? 0u
                                                                       : static_cast<uint16_t>(in));
    }
    if (!pins_.reassigned || (pins_.gpdr & (1u << kRxdPin)) == 0u) return reply;
    return clocked ? Sa11xxDmaReceiveFifo::kNoValue : PinWord(kRxdPin, fmt_.data_bits);
}

void Sa11xxSspStream::SettlePushes(uint64_t half) {
    const bool           valued = Looped() || device_ != nullptr || pins_.reassigned;
    std::vector<int32_t> values;
    uint64_t             n = 0;
    while (!frames_.empty() && frames_.front().push != kNever && frames_.front().push <= half) {
        if (valued) values.push_back(Received(frames_.front().value));
        frames_.pop_front();
        ++n;
    }
    rx_->Store(n, valued ? values.data() : nullptr);
}

void Sa11xxSspStream::SetSupply(uint64_t now, bool receive, uint64_t words) {
    Settle(now);
    const uint64_t supply = enabled_ ? words : 0u;
    if (receive) {
        rx_->SetSupply(supply);
    } else {
        tx_->SetSupply(supply);
        StartRun(now);
    }
    RefreshLine(now);
}

uint64_t Sa11xxSspStream::MovedOf(bool receive) const {
    return receive ? rx_->Moved() : tx_->Moved();
}

bool Sa11xxSspStream::FillReceived(uint32_t pa, uint32_t bytes) {
    return rx_->Fill(pa, bytes);
}

bool Sa11xxSspStream::Looped() const {
    return (sscr1_ & kLbm) != 0u;
}

GuestCycleClock::Rate Sa11xxSspStream::WordRate() const {
    const GuestCycleClock::Rate bit = bit_->BitRate();
    return bit.Scaled(2u, fmt_.period);
}

bool Sa11xxSspStream::TxCycleOfMoved(uint64_t words, uint64_t& cycle) {
    uint64_t n = 0;
    if (!tx_->TakesToMove(words, n)) return false;
    if (n == 0u) {
        cycle = clock_->Cycles();
        return true;
    }
    uint64_t tick = 0;
    if (!enabled_ || !bit_->Clocked() || !run_.NextLoadTick(n, fmt_.period, tick)) return false;
    cycle = bit_->CycleOfHalfTick(tick);
    return true;
}

bool Sa11xxSspStream::RxCycleOfMoved(uint64_t words, uint64_t& cycle) {
    uint64_t m = 0;
    if (!rx_->TakesToMove(words, m)) return false;
    if (m == 0u) {
        cycle = clock_->Cycles();
        return true;
    }
    uint64_t tick = 0;
    if (!PushTick(m, tick)) return false;
    cycle = bit_->CycleOfHalfTick(tick);
    return true;
}

bool Sa11xxSspStream::PushTick(uint64_t m, uint64_t& tick) const {
    if (m == 0u || !enabled_ || !bit_->Clocked()) return false;
    const uint64_t left = tx_->TakesBeforeEmpty();
    if (m <= frames_.size()) {
        const Frame& f = frames_[static_cast<size_t>(m - 1u)];
        if (f.push != kNever) {
            tick = f.push;
            return true;
        }
        const bool next = run_.Running() && left != 0u;
        tick = f.load + (next ? fmt_.push_cont : fmt_.push_last);
        return true;
    }
    const uint64_t q    = m - frames_.size();
    uint64_t       load = 0;
    if (q > left) return false;
    if (!run_.NextLoadTick(q, fmt_.period, load)) return false;
    const bool next = q < left;
    tick = load + (next ? fmt_.push_cont : fmt_.push_last);
    return true;
}

/* §11.12.12 SSSR (page 11-165): BSY "SSP is currently transmitting and/or receiving a frame"; TFS
   and RFS need "SSP operation is enabled"; ROR is the only read/write bit. */
uint32_t Sa11xxSspStream::Status(uint64_t now) {
    Settle(now);
    const uint32_t tx_level = tx_->Level();
    const uint32_t rx_level = rx_->Filled();
    uint32_t s = 0u;
    if (tx_level < kTxDepth) s |= kTnf;
    if (rx_level != 0u) s |= kRne;
    if (enabled_ && !frames_.empty()) s |= kBsy;
    if (enabled_ && tx_level <= kBurst) s |= kTfs;
    if (enabled_ && rx_level >= kBurst) s |= kRfs;
    if (rx_->Overrun()) s |= kRor;
    return s;
}

void Sa11xxSspStream::ClearOverrun(uint64_t now) {
    Settle(now);
    rx_->ClearOverrun();
    RefreshLine(now);
}

/* §11.12.12.4-6 (pages 11-164 / 11-165): TFS and RFS request an interrupt unless TIE / RIE is
   cleared; "When the ROR bit is set, an interrupt request is made." */
bool Sa11xxSspStream::LineLevel() const {
    if (rx_->Overrun()) return true;
    if (!enabled_) return false;
    const bool tfs = tx_->Level() <= kBurst;
    const bool rfs = rx_->Filled() >= kBurst;
    return (tfs && (sscr1_ & kTie) != 0u) || (rfs && (sscr1_ & kRie) != 0u);
}

bool Sa11xxSspStream::NextRise(uint64_t& cycle) const {
    cycle = kNever;
    if (!bit_->Clocked()) return false;
    uint64_t       at      = 0;
    const uint64_t tx_left = tx_->TakesBeforeEmpty();
    if ((sscr1_ & kTie) != 0u && tx_left > kBurst &&
        run_.NextLoadTick(tx_left - kBurst, fmt_.period, at)) {
        cycle = std::min(cycle, bit_->CycleOfHalfTick(at));
    }
    const uint64_t rx_left = rx_->TakesBeforeEmpty();
    const uint64_t rx_lead = kRxDepth - kBurst;
    if ((sscr1_ & kRie) != 0u && rx_left > rx_lead && PushTick(rx_left - rx_lead, at)) {
        cycle = std::min(cycle, bit_->CycleOfHalfTick(at));
    }
    if (!rx_->Overrun() && PushTick(rx_left + 1u, at)) {
        cycle = std::min(cycle, bit_->CycleOfHalfTick(at));
    }
    return cycle != kNever;
}

void Sa11xxSspStream::RefreshLine(uint64_t now) {
    const bool level = LineLevel();
    intc_->SetSourceLevel(kIntcBitSsp, level ? kIntcBitSsp : 0u);
    uint64_t cycle = 0;
    if (!level && enabled_ && NextRise(cycle)) {
        clock_->Arm(line_ev_, std::max(cycle, now));
    } else {
        clock_->Disarm(line_ev_);
    }
}

void Sa11xxSspStream::Save(StateWriter& w) {
    Settle(clock_->Cycles());
    w.Write<uint32_t>("stream_sscr0", sscr0_);
    w.Write<uint32_t>("stream_sscr1", sscr1_);
    w.Write<uint8_t>("stream_enabled", enabled_ ? 1u : 0u);
    w.Write<uint16_t>("stream_last_rx", last_rx_);
    bit_->Save(w);
    run_.Save(w);
    tx_->Save(w);
    rx_->Save(w);
    w.Write<uint32_t>("stream_frame_count", static_cast<uint32_t>(frames_.size()));
    for (const Frame& f : frames_) {
        w.Write("stream_frame_load", f.load);
        w.Write("stream_frame_push", f.push);
        w.Write("stream_frame_value", f.value);
    }
    if (device_) device_->SaveState(w);
}

void Sa11xxSspStream::Restore(StateReader& r) {
    uint8_t  enabled = 0;
    uint32_t n       = 0;
    r.Read("stream_sscr0", sscr0_);
    r.Read("stream_sscr1", sscr1_);
    r.Read("stream_enabled", enabled);
    r.Read("stream_last_rx", last_rx_);
    bit_->Restore(r);
    run_.Restore(r);
    tx_->Restore(r);
    rx_->Restore(r);
    frames_.clear();
    r.Read("stream_frame_count", n);
    for (uint32_t i = 0; i < n; ++i) {
        Frame f;
        r.Read("stream_frame_load", f.load);
        r.Read("stream_frame_push", f.push);
        r.Read("stream_frame_value", f.value);
        frames_.push_back(f);
    }
    enabled_ = enabled != 0u;
    fmt_     = Sa11xxSspFrameFormat::Of(sscr0_, sscr1_);
    if (device_) device_->RestoreState(r);
}

void Sa11xxSspStream::PostRestore(uint64_t now) {
    pins_ = ReadPins();
    if (device_) device_->PostRestore();
    RefreshLine(now);
}

REGISTER_SERVICE(Sa11xxSspStream);

}
