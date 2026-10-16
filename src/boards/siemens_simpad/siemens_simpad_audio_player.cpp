#include "../../socs/sa11xx/sa11xx_dma_audio_player.h"

#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include <cstdint>

namespace cerfcore {

namespace {

/* SIMpad SL4 wavedev PDD_AudioInitialize (sub_1331BF0) programs the playback
   DMA DDAR = 0x818002AA (MCP MCDR0 port 0x80060008, mono halfword write); the
   record DDAR 0x818002BB (...B0 after masking) is excluded by the match. Same
   SA-1110 MCP audio path as the Jornada 820 -> UCB1300 codec. */
constexpr uint32_t kMcpAudioTxDdarMask  = 0xFFFFFFF0u;
constexpr uint32_t kMcpAudioTxDdarValue = 0x818002A0u;

class SiemensSimpadAudioPlayer : public Sa11xxDmaAudioPlayer {
public:
    using Sa11xxDmaAudioPlayer::Sa11xxDmaAudioPlayer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

protected:
    Sa11xxAudioConfig AudioConfig() const override {
        return { kMcpAudioTxDdarMask, kMcpAudioTxDdarValue,
                 /*channels=*/1, /*bits=*/16, /*max_page=*/0x2000u,
                 /*allow_resampler=*/false, "SimpadAudio" };
    }
};

}  /* namespace */

REGISTER_SERVICE(SiemensSimpadAudioPlayer);

}
