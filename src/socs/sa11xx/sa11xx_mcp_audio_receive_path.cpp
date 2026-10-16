#include "sa11xx_mcp_audio_receive_path.h"

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

Sa11xxMcpAudioReceivePath::Sa11xxMcpAudioReceivePath(CerfEmulator& emu)
    : Sa11xxMcpReceivePath(emu) {}

REGISTER_SERVICE(Sa11xxMcpAudioReceivePath);

}
