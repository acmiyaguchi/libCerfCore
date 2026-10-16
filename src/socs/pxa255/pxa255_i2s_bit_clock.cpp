#include "pxa255_i2s_bit_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "pxa255_clock_manager.h"
#include "pxa255_gpio.h"
#include "pxa255_i2s_bit_clock_input.h"
#include "pxa255_id.h"

namespace {

/* Intel PXA255 Developer's Manual page 14-6: with BITCLK an output, SADIV divides the 147.46 MHz PLL clock
   into SYSCLK and SYSCLK / 4 is BITCLK (Table 14-2); Table 14-8 (page 14-13): SADIV 6:0, reset 0x1A. */
constexpr uint64_t kPllHz        = 147456000u;
constexpr uint64_t kSysclkPerBit = 4u;
constexpr uint32_t kSadivMask    = 0x7Fu;
constexpr uint32_t kSadivReset   = 0x1Au;
/* Intel PXA255 Developer's Manual Table 4-1 (page 4-3): GP28 ALT_FN_2_IN "I2S bit_clk"; section 14.2
   (page 14-2): the BITCLK direction is programmed in GPDR, GAFR and SACR0[BCKD]. */
constexpr uint32_t kBitClockPin   = 28u;
constexpr uint32_t kBitClockInput = 2u;
/* Intel PXA255 Developer's Manual Table 3-21 (page 3-37): CKEN8 "I2S Unit Clock Enable"; section 3.3.6
   (page 3-6): "When a module's clock is disabled, the registers in that module are still readable and
   writable." */
constexpr uint32_t kCkenI2s = 8u;

}  // namespace

REGISTER_SERVICE(Pxa255I2sBitClock);

bool Pxa255I2sBitClock::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

void Pxa255I2sBitClock::OnReady() {
    clock_  = &emu_.Get<GuestCycleClock>();
    clocks_ = &emu_.Get<Pxa255ClockManager>();
    gpio_   = &emu_.Get<Pxa255Gpio>();
    pad_    = &emu_.Get<Pxa255I2sBitClockInput>();
    sadiv_  = kSadivReset;
    clocks_->RegisterClockEnableListener([this](uint32_t) { Update(clock_->Cycles()); });
    gpio_->RegisterPinConfigListener([this] { Update(clock_->Cycles()); });
    pad_->SetChangeListener([this](uint64_t) { Update(clock_->Cycles()); });
    clock_->RegisterRateListener([this] { OnCpuRate(); });
}

void Pxa255I2sBitClock::Attach(std::function<Inputs()> inputs, std::function<void(uint64_t)> before,
                               std::function<void(uint64_t, bool)> after) {
    inputs_ = std::move(inputs);
    before_ = std::move(before);
    after_  = std::move(after);
}

/* Intel PXA255 Developer's Manual Table 14-3 (page 14-9) BCKD: "0 = Input. BITCLK driven by an external
   source. 1 = Output. BITCLK generated internally and driven out to the CODEC." */
GuestCycleClock::Rate Pxa255I2sBitClock::Selected() const {
    if (inputs_().bckd) {
        if (sadiv_ == 0u) return GuestCycleClock::Rate{0u, 1u};
        return GuestCycleClock::Rate{kPllHz, static_cast<uint64_t>(sadiv_) * kSysclkPerBit};
    }
    const Pxa2xxGpio::PinConfig pin = gpio_->Pin(kBitClockPin);
    if (pin.output || pin.function != kBitClockInput) return GuestCycleClock::Rate{0u, 1u};
    return pad_->PadClock();
}

void Pxa255I2sBitClock::Update(uint64_t now) {
    const GuestCycleClock::Rate r   = Selected();
    const bool                  run = inputs_().enabled && clocks_->ClockEnabled(kCkenI2s) && r.num != 0u;
    const bool                  was = running_;
    if (run == was && (!run || r.Reduced() == rate_.Reduced())) return;
    before_(now);
    if (was && run) {
        bits_.Rescale(now, clock_->ClockRate(), r);
        rate_ = r;
    } else if (was) {
        held_    = bits_.PositionAt(now);
        running_ = false;
    } else {
        rate_ = r;
        Place(now);
        running_ = true;
    }
    after_(now, was);
}

void Pxa255I2sBitClock::Place(uint64_t now) {
    bits_.SetRate(clock_->ClockRate(), rate_);
    bits_.PlaceAt(now, held_);
}

void Pxa255I2sBitClock::Restart(uint64_t now) {
    if (!running_) held_ = RatedTickCount::Position{};
    Update(now);
    if (!running_) held_ = RatedTickCount::Position{};
}

void Pxa255I2sBitClock::WriteSadiv(uint64_t now, uint32_t value) {
    sadiv_ = value & kSadivMask;
    Update(now);
}

void Pxa255I2sBitClock::ResetSadiv(uint64_t now) {
    sadiv_ = kSadivReset;
    Update(now);
}

void Pxa255I2sBitClock::OnCpuRate() {
    if (!running_) return;
    const uint64_t now = clock_->Cycles();
    before_(now);
    bits_.Rescale(now, clock_->ClockRate(), rate_);
    after_(now, true);
}

void Pxa255I2sBitClock::ResetLine() {
    running_ = false;
    held_    = RatedTickCount::Position{};
    rate_    = GuestCycleClock::Rate{0u, 1u};
    sadiv_   = kSadivReset;
}

void Pxa255I2sBitClock::Save(StateWriter& w) {
    const RatedTickCount::Position pos = running_ ? bits_.PositionAt(clock_->Cycles()) : held_;
    w.Write("sadiv", sadiv_);
    w.Write<uint8_t>("i2s_running", running_ ? 1u : 0u);
    w.Write<uint64_t>("i2s_bit_rate_num", rate_.num);
    w.Write<uint64_t>("i2s_bit_rate_den", rate_.den);
    w.Write<uint64_t>("i2s_bits_ticks", pos.ticks);
    w.Write<uint64_t>("i2s_bits_phase", pos.phase);
    w.Write<uint64_t>("i2s_bits_phase_den", pos.phase_den);
}

void Pxa255I2sBitClock::Restore(StateReader& r) {
    uint8_t                  running = 0;
    RatedTickCount::Position pos;
    r.Read("sadiv", sadiv_);
    r.Read("i2s_running", running);
    r.Read("i2s_bit_rate_num", rate_.num);
    r.Read("i2s_bit_rate_den", rate_.den);
    r.Read("i2s_bits_ticks", pos.ticks);
    r.Read("i2s_bits_phase", pos.phase);
    r.Read("i2s_bits_phase_den", pos.phase_den);
    running_ = running != 0u;
    held_    = running_ ? RatedTickCount::Position{} : pos;
    if (!running_) return;
    bits_.SetRate(clock_->ClockRate(), rate_);
    bits_.PlaceAt(clock_->Cycles(), pos);
}

void Pxa255I2sBitClock::PostRestore() { Update(clock_->Cycles()); }
