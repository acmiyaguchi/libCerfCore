#define NOMINMAX

#include "../freescale_sdma_audio_player.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx31_id.h"

namespace cerfcore {

namespace {

class Imx31AudioPlayer : public FreescaleSdmaAudioPlayer {
public:
    using FreescaleSdmaAudioPlayer::FreescaleSdmaAudioPlayer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }

protected:
    /* zune_keel wavedev_wm8978.dll render loops sub_30E5B64 / sub_30E5C88 / sub_30E5F10
       write the DMA buffer as interleaved 16-bit stereo frames. */
    FreescaleAudioFormat StreamFormat(uint32_t ssi) override {
        return FreescaleAudioFormat{SampleRateHz(ssi, 2u), 2u, 16u};
    }

    const char* LogTag() const override { return "iMX31-Audio"; }
};

}

REGISTER_SERVICE(Imx31AudioPlayer);

}
