#include "../../socs/sa11xx/sa11xx_dma_audio_player.h"

#include <cerfcore/boards/board_context.h>
#include "compaq_ipaq_h3000_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_ipaq_h3000_audio_amp.h"

#include <cstdint>

namespace {

/* iPAQ H3600 SSP audio transmit DMA: DA[31:8]=0x81C01B (SSP data port), wavedev
   TX DDAR = 0x81C01BE8, RW (bit 0) = 0 = transmit (sub_F51924). */
constexpr uint32_t kDdarSspTxMask  = 0xFFFFFF01u;
constexpr uint32_t kDdarSspTxValue = 0x81C01B00u;

class CompaqIpaqH3000AudioPlayer : public Sa11xxDmaAudioPlayer {
public:
    using Sa11xxDmaAudioPlayer::Sa11xxDmaAudioPlayer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
    }

protected:
    Sa11xxAudioConfig AudioConfig() const override {
        return { kDdarSspTxMask, kDdarSspTxValue,
                 /*channels=*/2, /*bits=*/16, /*max_page=*/16384u,
                 /*allow_resampler=*/false, "CompaqIpaqH3000Audio" };
    }

    bool OutputMuted() const override {
        return !emu_.Get<CompaqIpaqH3000AudioAmp>().Powered();
    }
};

}  /* namespace */

REGISTER_SERVICE(CompaqIpaqH3000AudioPlayer);
