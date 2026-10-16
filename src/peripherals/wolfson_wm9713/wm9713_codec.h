#pragma once

#include "../wolfson_wm97xx/wm97xx_codec.h"

#include <cstdint>

namespace cerfcore {

class Wm9713Clocks;

/* symbol_mk500 touch.dll FUN_02238118 @0x02238118 names this part
   "WM9713/14" and matches it on device ID 0x4C13. */
class Wm9713Codec : public Wm97xxCodec {
public:
    using Wm97xxCodec::Wm97xxCodec;

    bool ShouldRegister() override;
    void OnReady() override;

    uint16_t ReadReg(uint32_t reg, uint64_t frame) override;
    void     WriteReg(uint32_t reg, uint16_t value, uint64_t frame) override;

    void ColdReset() override;
    void LinkStarted(uint64_t frame) override;
    bool DacPowered() override;
    bool AdcPowered() override;
    uint32_t PcmOutChannels() override;
    bool     PcmOutEveryFrame() override;

    uint16_t GpioSlotStatus(uint64_t frame) override;

    GuestCycleClock::Rate BitClockRate() override;
    uint32_t              ConverterDivider() override;
    uint32_t              AdcLeftSlot() override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    uint16_t Peek(uint32_t reg) override;
    void     Poke(uint32_t reg, uint16_t value) override;
    const uint16_t* SupportedRates(uint32_t& count) const override;
    bool            IsRateRegister(uint32_t reg) const override;
    bool            RateWriteTakesEffect(uint32_t reg) override;
    uint16_t ConversionData(uint8_t tag) override;
    void     Reconfigure(uint64_t frame, bool poll) override;
    void     OnPenSignal(uint64_t frame, uint64_t now) override;

private:
    static void     AddCoordinateSet(Wm97xxDigitiser::Sequence& seq, uint8_t extra);

    bool PrClear(uint16_t pr_bit) const;
    void ResetRegisters();
    void RequireRegister(uint32_t reg);
    bool PenXnor(uint64_t frame) const;
    void EvaluateGpio(uint64_t frame, uint64_t now);

    static constexpr uint32_t kNumRegs = 0x80u;
    Wm9713Clocks* clocks_ = nullptr;
    uint16_t reg_[kNumRegs] = {};
    bool     stalled_poll_  = false;
    bool     pen_q_         = false;
    uint64_t pen_q_frame_   = 0;
    bool     gpio_int_      = false;
};

}
