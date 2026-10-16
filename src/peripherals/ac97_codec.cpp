#include "ac97_codec.h"

#include <cerfcore/state/state_stream.h>

namespace {

constexpr uint16_t kFixedRate = 0xBB80u;

}  // namespace

/* AC '97 Component Specification Revision 2.1 section 7 (page 47): a warm
   reset "will restart AC '97's digital interface (resetting PR4 to zero)". */
void Ac97Codec::WarmReset() {
    Poke(kRegPowerdown, static_cast<uint16_t>(Peek(kRegPowerdown) & ~kPr4));
}

/* AC '97 Component Specification Revision 2.1 Table 19: "PR4 Digital Interface
   (AC-link) powerdown (external clk off)". */
bool Ac97Codec::LinkPoweredDown() {
    return (Peek(kRegPowerdown) & kPr4) != 0u;
}

bool Ac97Codec::IsRateRegister(uint32_t reg) const { return reg == kRegDacRate || reg == kRegAdcRate; }

/* AC '97 Component Specification Revision 2.2 section 5.8.3 (page 61): "if the value written to the
   register is supported that value will be echoed back when read, otherwise the closest (higher in
   case of a tie) sample rate supported is returned". */
uint16_t Ac97Codec::NearestSupported(uint16_t value) const {
    uint32_t        count = 0;
    const uint16_t* rates = SupportedRates(count);
    uint16_t        best  = rates[0];
    for (uint32_t i = 1; i < count; ++i) {
        const uint32_t d    = value > rates[i] ? value - rates[i] : rates[i] - value;
        const uint32_t dbest = value > best ? value - best : best - value;
        if (d < dbest || (d == dbest && rates[i] > best)) best = rates[i];
    }
    return best;
}

/* AC '97 Component Specification Revision 2.2 section 5.8.3 (page 61): with VRA 0 "the registers are forced
   to BB80h". */
bool Ac97Codec::WriteRateRegister(uint32_t reg, uint16_t value) {
    if (!IsRateRegister(reg)) return false;
    if ((Peek(kRegExtAudioCtrl) & kVra) == 0u) {
        Poke(reg, kFixedRate);
        return true;
    }
    const uint16_t latched = NearestSupported(value);
    Poke(reg, latched);
    if (!RateWriteTakesEffect(reg)) return true;
    if (reg == kRegDacRate) dac_rate_ = latched;
    if (reg == kRegAdcRate) adc_rate_ = latched;
    return true;
}

bool Ac97Codec::RateWriteTakesEffect(uint32_t) { return true; }

void Ac97Codec::OnExtAudioCtrlWrite() {
    if ((Peek(kRegExtAudioCtrl) & kVra) != 0u) return;
    for (uint32_t reg = kRegDacRate; reg <= kRegAdcRate; reg += 2u) {
        if (IsRateRegister(reg)) Poke(reg, kFixedRate);
    }
    ResetRates();
}

void Ac97Codec::ResetRates() {
    dac_rate_ = kFrameRateHz;
    adc_rate_ = kFrameRateHz;
}

void Ac97Codec::SaveRates(StateWriter& w) const {
    w.Write<uint32_t>("codec_dac_rate", dac_rate_);
    w.Write<uint32_t>("codec_adc_rate", adc_rate_);
}

void Ac97Codec::RestoreRates(StateReader& r) {
    r.Read("codec_dac_rate", dac_rate_);
    r.Read("codec_adc_rate", adc_rate_);
}

uint16_t Ac97Codec::GpioSlotStatus(uint64_t) { return 0u; }

/* AC '97 Component Specification Revision 2.1 Figure 13 (page 32): BIT_CLK 12.288 MHz. */
GuestCycleClock::Rate Ac97Codec::BitClockRate() { return GuestCycleClock::Rate{kBitClockHz, 1u}; }

uint32_t Ac97Codec::ConverterDivider() { return 1u; }

/* Intel PXA255 Developer's Manual section 13.4.2.4 (page 13-11): "Slot 3 contains the CODEC left channel
   output". */
uint32_t Ac97Codec::AdcLeftSlot() { return 3u; }

uint32_t Ac97Codec::DigitiserSlot() { return 0u; }
