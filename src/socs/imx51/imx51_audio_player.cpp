#define NOMINMAX

#include "../freescale_sdma_audio_player.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../freescale_ssi_external_clock.h"
#include "imx51_id.h"

namespace {

class Imx51AudioPlayer : public FreescaleSdmaAudioPlayer {
public:
    using FreescaleSdmaAudioPlayer::FreescaleSdmaAudioPlayer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx51;
    }

protected:
    FreescaleAudioFormat StreamFormat(uint32_t ssi) override {
        FreescaleAudioFormat format = emu_.Get<FreescaleSsiExternalClock>().Format(ssi);
        format.rate_hz              = SampleRateHz(ssi, format.channels);
        return format;
    }

    const char* LogTag() const override { return "iMX51-Audio"; }
};

}

REGISTER_SERVICE(Imx51AudioPlayer);
