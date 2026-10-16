#include "sa11xx_mcp_channel.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa11xx_dma.h"
#include "sa11xx_dma_transmit_fifo.h"
#include "sa11xx_mcp_frame_clock.h"
#include "sa11xx_mcp_regs.h"
#include "sa11xx_ppc.h"

#include <algorithm>
#include <vector>

namespace cerfcore {

namespace {

using namespace sa11xx_mcp;

constexpr uint64_t kNever = UINT64_MAX;

/* SA-1110 §11.12.1.2 Figure 11-31 (printed 11-129): "a divisor value of 12 is used, causing the
   counter to decrement to zero after 384 (32*12=384) SCLK cycles occur". */
constexpr uint64_t kTicksPerStep = 32u;

/* Linux ucb1x00 UCB_AC_B / UCB_TC_B *_IN_ENA (1 << 14), *_OUT_ENA (1 << 15); SA-1110 §11.12.6.15 /
   §11.12.6.16: register 8 / register 6 written with bit 14 or 15 set. */
constexpr uint16_t kCodecInEna   = 0x4000u;
constexpr uint16_t kCodecEnables = 0xC000u;

}

bool Sa11xxMcpChannel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxMcpChannel::OnReady() {
    clock_  = &emu_.Get<GuestCycleClock>();
    dma_    = &emu_.Get<Sa11xxDma>();
    frames_ = &emu_.Get<Sa11xxMcpFrameClock>();
    period_ = PeriodOf(mccr0_, emu_.Get<Sa11xxPpc>().Mccr1());
    tx_->Attach(&tx_port_);
    rx_->Attach(&rx_port_);
    dma_->RegisterPort(cfg_.ds_transmit, &tx_port_);
    dma_->RegisterPort(cfg_.ds_receive, &rx_port_);
    /* §11.12.3.11 (printed 11-137): "the MCE bit is the only control bit that is reset to a known
       state"; MCSR reset row (printed 11-149): ATU, ARO, TTU and TRO "?"; §11.12.5.1 (printed
       11-141): the FIFOs "are cleared when the SA-1110 is reset". */
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        Disable();
        mccr0_ &= ~kMce;
        underrun_ = false;
        rx_->ClearOverrun();
    });
}

uint64_t Sa11xxMcpChannel::Port::Moved() const {
    return receive_ ? c_.rx_->Moved() : c_.tx_->Moved();
}

bool Sa11xxMcpChannel::Mce() const {
    return (mccr0_ & kMce) != 0u;
}

bool Sa11xxMcpChannel::Looped() const {
    return (mccr0_ & kLbm) != 0u;
}

bool Sa11xxMcpChannel::Adm() const {
    return (mccr0_ & kAdm) != 0u;
}

Sa11xxMcpReceivePath::LoopFuture Sa11xxMcpChannel::Future() const {
    return Sa11xxMcpReceivePath::LoopFuture{&counter_, Period(), tx_->TakesBeforeEmpty(), Adm()};
}

/* SA-1110 §11.12.3 MCCR0 (printed 11-137): ASD "Value (from 6 to 127)", TSD "Value (from 16 to
   127)", "Sample Rate = Programmed clock rate/(32xASD)"; §11.12.3.2 (printed 11-134): "Unpredictable
   results occur for TSD values smaller than 16". */
uint64_t Sa11xxMcpChannel::PeriodOf(uint32_t mccr0, uint32_t mccr1) const {
    const uint32_t divisor = (mccr0 >> cfg_.divisor_shift) & kDivisorMask;
    if (divisor >= cfg_.divisor_min) return kTicksPerStep * divisor;
    if (cfg_.low_rate_hz == 0u) return kTicksPerStep * cfg_.divisor_min;
    GuestCycleClock::Rate sclk{Sa11xxMcpFrameClock::SclkHzFor(mccr1), 1u};
    frames_->SclkRateFor(mccr0, mccr1, sclk);
    const uint64_t per_sample = sclk.den * cfg_.low_rate_hz;
    return (sclk.num + per_sample / 2u) / per_sample;
}

GuestCycleClock::Rate Sa11xxMcpChannel::WordRate(bool receive) const {
    const GuestCycleClock::Rate sclk  = frames_->WordClock();
    const uint64_t              ticks = receive && !Adm() ? kFrameTicks : Period();
    return sclk.Scaled(1u, ticks);
}

void Sa11xxMcpChannel::Disable() {
    codec_b_   = 0u;
    decoded_b_ = 0u;
    counter_.Reset();
    rx_->Reset();
    ClearFifos();
}

void Sa11xxMcpChannel::ClearFifos() {
    tx_->Clear();
    rx_->ClearFifo();
}

/* §11.12.3.3 (printed 11-135): MCE clear causes "the current frame, which is being transmitted, to
   be terminated"; "Clearing MCE resets the MCP's FIFOs." */
void Sa11xxMcpChannel::Freeze() {
    rx_->Freeze();
    ClearFifos();
}

void Sa11xxMcpChannel::PrepareControl(uint64_t now, uint32_t mccr0, uint32_t mccr1) {
    const uint64_t period  = PeriodOf(mccr0, mccr1);
    const bool     adm     = (mccr0 & kAdm) != 0u;
    const bool     was_adm = Adm();
    if (counter_.Counting() && period != Period()) {
        const uint64_t at = counter_.Rephase(Period());
        rx_->Rephase(was_adm, at, Period(), period, frames_->SclkNow(now));
    }
    if ((frames_->Enabled() || frames_->Held()) && adm != was_adm) {
        rx_->SwitchMode(adm, frames_->NextFrameEdge(now) - kFrameTicks, period,
                        (codec_b_ & kCodecInEna) != 0u, (codec_b_ & kCodecEnables) != 0u, counter_);
    }
    was_looped_ = Looped();
    mccr0_      = mccr0;
    period_     = period;
}

void Sa11xxMcpChannel::Resume(uint64_t frozen, uint64_t base) {
    counter_.Shift(frozen, base - frozen);
    rx_->Resume(frozen, base, Adm(), Period(), counter_);
}

void Sa11xxMcpChannel::FinishControl(uint64_t now) {
    if (Looped() == was_looped_) return;
    rx_->SetLooped(Looped(), frames_->NextFrameEdge(now), Adm(), Period(), counter_);
}

/* §11.12.3.1 (printed 11-133): the sample clock starts at "The rising edge of the next SFRM pulse
   after the write has been made"; §11.12.3.2 (printed 11-134): the telecom clock "works exactly
   like the audio sample rate clock". */
bool Sa11xxMcpChannel::CodecWrite(uint64_t now, uint64_t carry, uint32_t reg, uint16_t value,
                                  bool looped) {
    if (reg != cfg_.codec_reg) return false;
    const uint16_t before = decoded_b_;
    const bool     was_in = (codec_b_ & kCodecInEna) != 0u;
    decoded_b_ = value;
    if (!looped) codec_b_ = value;
    const bool on     = (value & kCodecEnables) != 0u;
    const bool was_on = (before & kCodecEnables) != 0u;
    const bool in     = (codec_b_ & kCodecInEna) != 0u;
    if (!on && !was_on) return false;
    Settle(now);
    const uint64_t edge = carry + kFrameTicks;
    /* Figure 11-31 (printed 11-129): the counters "start to decrement on the next SFRM pulse"; page
       11-130: "The SFRM pulse following the write causes the disable to take effect". */
    if (on) {
        if (counter_.Counting()) counter_.Reload(edge);
        else                     counter_.Enable(edge);
    } else {
        counter_.Disable(edge);
    }
    if (!looped) rx_->OnCodecWrite(Adm(), on, in, was_in, carry, Period());
    line_(now);
    return true;
}

/* §11.12.1.3 (printed 11-130): the reload "triggers the audio transmit FIFO to transfer the next
   available entry"; §11.12.6.5 / §11.12.6.7: an underrun when the transmit logic "attempts to fetch
   data from the FIFO after it has been completely emptied". */
void Sa11xxMcpChannel::Settle(uint64_t now) {
    if (!frames_->Enabled()) return;
    const uint64_t t = frames_->SclkNow(now);
    std::vector<uint64_t> zeros;
    uint64_t zero = 0;
    if (Looped()) {
        for (uint64_t k = 1u; counter_.TakeTick(k, Period(), zero) && zero <= t; ++k) {
            zeros.push_back(zero);
        }
    }
    const uint64_t takes  = counter_.Settle(t, Period());
    const uint16_t before = tx_->Shifter();
    std::vector<uint16_t> taken;
    if (takes != 0u && tx_->Take(takes, taken) != 0u) underrun_ = true;
    if (Looped()) {
        if (zeros.size() != takes) {
            emu_.Get<Fatal>().Die("Sa11xxMcp: %llu takes settled at SCLK %llu, %zu listed",
                                  static_cast<unsigned long long>(takes),
                                  static_cast<unsigned long long>(t), zeros.size());
        }
        rx_->LoopTakes(zeros, taken, before, Adm());
    }
    rx_->Settle(t, counter_.Counting());
}

/* §11.12.6.1 (printed 11-145): "The state of ATS is also sent to the DMA controller"; MCSR (printed
   11-149): ATS, ARS, TTS and TRS are 0 when the "MCP disabled". */
void Sa11xxMcpChannel::SetSupply(uint64_t now, bool receive, uint64_t words) {
    Settle(now);
    if (receive) rx_->SetSupply(Mce() ? words : 0u);
    else         tx_->SetSupply(Mce() ? words : 0u);
    line_(now);
}

bool Sa11xxMcpChannel::TakeTick(uint64_t take, uint64_t& tick) const {
    return frames_->Enabled() && counter_.TakeTick(take, Period(), tick);
}

bool Sa11xxMcpChannel::TxCycleOfMoved(uint64_t words, uint64_t& cycle) {
    uint64_t n = 0, tick = 0;
    if (!tx_->TakesToMove(words, n)) return false;
    if (n == 0u) {
        cycle = clock_->Cycles();
        return true;
    }
    if (!TakeTick(n, tick)) return false;
    cycle = frames_->CycleOfSclk(tick);
    return true;
}

bool Sa11xxMcpChannel::RxCycleOfMoved(uint64_t words, uint64_t& cycle) {
    uint64_t tick = 0;
    bool     now  = false;
    if (!rx_->TickOfMoved(words, Future(), tick, now)) return false;
    if (now) {
        cycle = clock_->Cycles();
        return true;
    }
    if (!frames_->Enabled()) return false;
    cycle = frames_->CycleOfSclk(tick);
    return true;
}

/* MCSR (printed 11-149): ATS, ARS, TTS and TRS are 1 with the FIFO condition "and MCP operation is
   enabled"; §11.12.6.15 / §11.12.6.16 (printed 11-148 / 11-149): ACE and TCE set when "SFRM is
   asserted to indicate the start of the next frame". */
uint32_t Sa11xxMcpChannel::Status(uint64_t now) {
    Settle(now);
    const uint32_t tx_level = tx_->Level();
    const uint32_t rx_level = rx_->Filled();
    uint32_t s = 0u;
    if (Mce() && tx_level <= kRequestAt) s |= cfg_.ts;
    if (Mce() && rx_level >= kRequestAt) s |= cfg_.rs;
    if (underrun_) s |= cfg_.tu;
    if (rx_->Overrun()) s |= cfg_.ro;
    if (tx_level < kTxDepth) s |= cfg_.nf;
    if (rx_level != 0u) s |= cfg_.ne;
    if (counter_.Active(frames_->SclkNow(now))) s |= cfg_.ce;
    return s;
}

/* §11.12.6 (printed 11-145): "Writing a one to a sticky status bit clears it; writing a zero has no
   effect." */
void Sa11xxMcpChannel::ClearStatus(uint64_t now, uint32_t mask) {
    Settle(now);
    if ((mask & cfg_.tu) != 0u) underrun_ = false;
    if ((mask & cfg_.ro) != 0u) rx_->ClearOverrun();
    line_(now);
}

/* §11.12.5.1 (printed 11-141): "When MCDR0 is written, the topmost entry of the audio transmit FIFO
   is accessed"; §11.12.5.2 (printed 11-142): "When MCDR1 is written, the topmost entry of the
   telecom transmit FIFO is accessed". */
void Sa11xxMcpChannel::WriteData(uint64_t now, uint32_t value) {
    Settle(now);
    if (!Mce() || tx_->Level() >= kTxDepth) return;
    tx_->Put(static_cast<uint16_t>(value));
    if (!Looped()) {
        uint8_t word[2];
        cerf::le::Put16(word, static_cast<uint16_t>(value & tx_->DataMask()));
        dma_->TransmitData((cfg_.device_address << 8) | (cfg_.ds_transmit << 4), word, 2u,
                           WordRate(false));
    }
    line_(now);
}

uint32_t Sa11xxMcpChannel::ReadData(uint64_t now) {
    Settle(now);
    const uint16_t value = rx_->Pop();
    line_(now);
    return value;
}

/* §11.12.6 (printed 11-145): "A bit that can cause an interrupt signals the interrupt request as long
   as the bit is set"; ATS: "an interrupt request is made unless ... (ATE) bit is cleared"; the
   underrun and overrun bits are "nonmaskable" (§11.12.6.5 to §11.12.6.8). */
bool Sa11xxMcpChannel::LineLevel() const {
    if (underrun_ || rx_->Overrun()) return true;
    if (!Mce()) return false;
    const bool ts = tx_->Level() <= kRequestAt;
    const bool rs = rx_->Filled() >= kRequestAt;
    return (ts && (mccr0_ & cfg_.transmit_enable) != 0u) ||
           (rs && (mccr0_ & cfg_.receive_enable) != 0u);
}

bool Sa11xxMcpChannel::NextRise(uint64_t& tick) const {
    if (!frames_->Enabled()) return false;
    tick = kNever;
    uint64_t at = 0;
    const uint64_t tx_left = tx_->TakesBeforeEmpty();
    if ((mccr0_ & cfg_.transmit_enable) != 0u && tx_left > kRequestAt &&
        TakeTick(tx_left - kRequestAt, at)) {
        tick = std::min(tick, at);
    }
    if (!underrun_ && TakeTick(tx_left + 1u, at)) tick = std::min(tick, at);
    if (rx_->NextRise((mccr0_ & cfg_.receive_enable) != 0u, kRequestAt, Future(), at)) {
        tick = std::min(tick, at);
    }
    return tick != kNever;
}

void Sa11xxMcpChannel::Save(StateWriter& w) {
    Settle(clock_->Cycles());
    w.Write<uint8_t>("underrun", underrun_ ? 1u : 0u);
    w.Write<uint32_t>("mccr0", mccr0_);
    w.Write<uint64_t>("period", period_);
    w.Write<uint16_t>("codec_b", codec_b_);
    w.Write<uint16_t>("decoded_b", decoded_b_);
    counter_.Save(w);
    tx_->Save(w);
    rx_->Save(w);
}

void Sa11xxMcpChannel::Restore(StateReader& r) {
    uint8_t underrun = 0;
    r.Read("underrun", underrun);
    r.Read("mccr0", mccr0_);
    r.Read("period", period_);
    r.Read("codec_b", codec_b_);
    r.Read("decoded_b", decoded_b_);
    counter_.Restore(r);
    tx_->Restore(r);
    rx_->Restore(r);
    underrun_ = underrun != 0u;
}

}
