#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "imx28_apbx.h"
#include "imx28_clkctrl.h"
#include "imx28_digctl.h"
#include "imx28_saif.h"
#include "imx28_saif_host_input.h"
#include "imx28_saif_regs.h"

#include <cstdint>
#include <cstring>

namespace cerfcore {

using namespace imx28_saif;

/* Section 35.3.1 RUN: transmit keeps BITCLK and LRCLK running while RUN is set. */
bool Imx28Saif::DrivesLrclk() const {
    return (ctrl_ & (kCtrlRun | kCtrlReadMode)) == kCtrlRun && !Gated(ctrl_);
}

/* Section 35.2.1: sample rate is saif_clk over 512 (BITCLK_BASE_RATE 0) or 384 (1). */
GuestCycleClock::Rate Imx28Saif::FrameRate() const {
    const uint32_t over = (ctrl_ & kCtrlBaseRate) != 0u ? 384u : 512u;
    return emu_.Get<Imx28Clkctrl>().SaifRate(unit_).Scaled(1u, over).Reduced();
}

/* Table 35-2 HW_DIGCTL_CTRL_SAIF_CLKMUX_SEL. */
Imx28Saif* Imx28Saif::ClockSource() const {
    const uint32_t mux = digctl_->SaifClkmux();
    uint32_t src = unit_;
    if (mux == 1u) src = 1u - unit_;
    if (mux == 2u) src = 0u;
    if (mux == 3u) src = 1u;
    return src == unit_ ? const_cast<Imx28Saif*>(this) : peer_;
}

/* Section 35.2.3: slave receive samples while the BITCLK and LRCLK inputs transition. */
bool Imx28Saif::Receiving() const {
    if ((ctrl_ & (kCtrlRun | kCtrlReadMode)) != (kCtrlRun | kCtrlReadMode) || Gated(ctrl_)) return false;
    const Imx28Saif* src = ClockSource();
    return src != this && src->DrivesLrclk();
}

/* Section 35.3.1 SLAVE_MODE [2], WORD_LENGTH [7:4], CHANNEL_NUM_SELECT [15:14]; Table 35-2. */
void Imx28Saif::CheckReceive(uint32_t next, uint32_t addr, uint32_t value) const {
    if ((next & kCtrlSlaveMode) == 0u) Die("RUN in receive master mode", addr, value);
    if ((next & (kCtrlWordLength | kCtrlChannelMask)) != 0u) Die("RUN receive format", addr, value);
    if (ClockSource() == this) Die("RUN receive with clocks from this unit's pins", addr, value);
}

void Imx28Saif::RxSync() {
    if (rx_live_ == 0u) return;
    const uint64_t now = frames_.Now();
    if (now > rx_mark_) RxPush(now - rx_mark_);
    rx_mark_ = now;
}

void Imx28Saif::RxUpdate() {
    const bool now = Receiving();
    if (now && rx_live_ == 0u) {
        const GuestCycleClock::Rate fs = ClockSource()->FrameRate();
        frames_.SetOscRate(fs.num, fs.den);
        mic_->Open(static_cast<uint32_t>(fs.RoundedHz()));
        mic_->Clear();
        rx_mark_ = frames_.Now();
    }
    rx_live_ = now ? 1u : 0u;
    RxArm();
}

void Imx28Saif::RxArm() {
    clock_->Disarm(rx_event_);
    if (rx_live_ == 0u || rx_want_ == 0u || rx_dma_.size() >= rx_want_) return;
    frames_.ArmAt(rx_event_, rx_mark_ + (rx_want_ - rx_dma_.size()) / kFrameBytes);
}

/* Section 35.2.3: one FIFO entry per LRCLK frame; a push to a full FIFO is discarded and sets
   FIFO_OVERFLOW_IRQ (section 35.3.2). */
void Imx28Saif::RxPush(uint64_t frames) {
    if (rx_want_ != 0u) {
        const uint64_t need = (rx_want_ - rx_dma_.size()) / kFrameBytes;
        const uint64_t n    = frames < need ? frames : need;
        const size_t   at   = rx_dma_.size();
        std::vector<uint32_t> entries(n);
        mic_->Take(entries.data(), n);
        rx_dma_.resize(at + n * kFrameBytes);
        if (n != 0u) std::memcpy(rx_dma_.data() + at, entries.data(), n * kFrameBytes);
        frames -= n;
    }
    const uint64_t space = kRxFifoEntries - rx_count_;
    const uint64_t n     = frames < space ? frames : space;
    mic_->Take(rx_fifo_ + rx_count_, n);
    rx_count_ += static_cast<uint32_t>(n);
    frames -= n;
    if (frames == 0u) return;
    stat_ |= kStatOverflow;
    mic_->Take(nullptr, frames);
}

void Imx28Saif::OnRxEvent() {
    RxSync();
    if (rx_want_ != 0u && rx_dma_.size() == rx_want_) {
        apbx_->NotifyTransferDone(kDmaChannelBase + unit_);
        return;
    }
    RxArm();
}

void Imx28Saif::ResetRx() {
    rx_live_  = 0u;
    rx_mark_  = 0u;
    rx_count_ = 0u;
    rx_want_  = 0u;
    std::memset(rx_fifo_, 0, sizeof(rx_fifo_));
    rx_dma_.clear();
    clock_->Disarm(rx_event_);
}

/* Section 35.3.3: receive reads pop the bottom of the FIFO; an empty FIFO reads zero and sets
   FIFO_UNDERFLOW_IRQ (section 35.3.2). */
uint32_t Imx28Saif::ReadData(uint32_t addr) {
    if ((ctrl_ & kCtrlReadMode) == 0u) Die("DATA read in transmit mode", addr, 0u);
    RxSync();
    if (rx_count_ == 0u) {
        stat_ |= kStatUnderflow;
        return 0u;
    }
    const uint32_t v = rx_fifo_[0];
    std::memmove(rx_fifo_, rx_fifo_ + 1, (kRxFifoEntries - 1u) * sizeof(rx_fifo_[0]));
    --rx_count_;
    return v;
}

/* Section 35.2.4 DMA requests for received PCM; section 35.2.5 FIFO drained bottom first. */
Imx28ApbDmaDevice::Transfer Imx28Saif::RxDma(uint8_t* dst, uint32_t count) {
    if (rx_want_ != 0u) {
        if (count != rx_want_ || rx_dma_.size() != rx_want_) Die("DMA receive, bytes", base_, count);
        std::memcpy(dst, rx_dma_.data(), count);
        rx_dma_.clear();
        rx_want_ = 0u;
        return Transfer{count, false, false};
    }
    if ((count % kFrameBytes) != 0u || !Receiving()) Die("DMA receive with no frames, bytes", base_, count);
    RxSync();
    const uint32_t take = rx_count_ < count / kFrameBytes ? rx_count_ : count / kFrameBytes;
    rx_dma_.assign(reinterpret_cast<const uint8_t*>(rx_fifo_),
                   reinterpret_cast<const uint8_t*>(rx_fifo_ + take));
    std::memmove(rx_fifo_, rx_fifo_ + take, (kRxFifoEntries - take) * sizeof(rx_fifo_[0]));
    rx_count_ -= take;
    rx_want_ = count;
    if (rx_dma_.size() == count) return RxDma(dst, count);
    RxArm();
    return Transfer{0u, false, true};
}

void Imx28Saif::SaveRx(StateWriter& w) {
    w.Write("rx_live", rx_live_);
    w.Write("rx_mark", rx_mark_);
    w.Write("rx_fifo", rx_fifo_);
    w.Write("rx_count", rx_count_);
    w.Write("rx_want", rx_want_);
    w.Write("rx_dma_size", static_cast<uint32_t>(rx_dma_.size()));
    w.WriteBytes("rx_dma", rx_dma_.data(), rx_dma_.size());
}

void Imx28Saif::RestoreRx(StateReader& r) {
    uint32_t size = 0u;
    r.Read("rx_live", rx_live_);
    r.Read("rx_mark", rx_mark_);
    r.Read("rx_fifo", rx_fifo_);
    r.Read("rx_count", rx_count_);
    r.Read("rx_want", rx_want_);
    r.Read("rx_dma_size", size);
    rx_dma_.resize(size);
    r.ReadBytes("rx_dma", rx_dma_.data(), rx_dma_.size());
}

}
