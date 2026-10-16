#include "compaq_ipaq_h3000_audio_amp.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_ipaq_h3000_egpio.h"
#include "compaq_ipaq_h3000_family_id.h"

namespace cerfcore {

namespace {

class CompaqIpaqH3000EgpioAudioAmp : public CompaqIpaqH3000AudioAmp {
public:
    using CompaqIpaqH3000AudioAmp::CompaqIpaqH3000AudioAmp;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() ==
               BoardFamilyId::CompaqIpaqH3000;
    }

    /* Bit 10 (0x400) AUD_ON "Enables power to audio output amp", O(H): NetBSD
       sys/arch/hpcarm/dev/ipaq_gpioreg.h. */
    bool Powered() const override {
        return (emu_.Get<CompaqIpaqH3000Egpio>().Latched() &
                CompaqIpaqH3000Egpio::kAudioOutputEnable) != 0;
    }
};

}

REGISTER_SERVICE_AS_FALLBACK(CompaqIpaqH3000EgpioAudioAmp, CompaqIpaqH3000AudioAmp);

}
