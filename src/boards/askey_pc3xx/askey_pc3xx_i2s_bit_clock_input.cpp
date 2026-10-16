#include "../../socs/pxa255/pxa255_i2s_bit_clock_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/pxa2xx/pxa2xx_ac97_bit_clock.h"
#include <cerfcore/boards/board_context.h>
#include "askey_pc3xx_family_id.h"

namespace {

class AskeyPc3xxI2sBitClockInput : public Pxa255I2sBitClockInput {
public:
    using Pxa255I2sBitClockInput::Pxa255I2sBitClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::AskeyPc3xx;
    }

    void OnReady() override {
        bit_clock_ = &emu_.Get<Pxa2xxAc97BitClock>();
        bit_clock_->AddPadListener([this](uint64_t at) { Changed(at); });
    }

    /* Intel PXA255 Developer's Manual Table 4-1 (page 4-3): GP28 BITCLK is ALT_FN_1_IN "AC97 bit_clk" and
       ALT_FN_2_IN "I2S bit_clk", one pad. */
    GuestCycleClock::Rate PadClock() override { return bit_clock_->PadClock(); }

private:
    Pxa2xxAc97BitClock* bit_clock_ = nullptr;
};

}

REGISTER_SERVICE_AS(AskeyPc3xxI2sBitClockInput, Pxa255I2sBitClockInput);
