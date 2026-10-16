#include "sa11xx_mcp_audio_transmit_path.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_mcp_regs.h"

using namespace sa11xx_mcp;

namespace {

/* SA-1110 §11.12.5.1 MCDR0 (printed 11-142): bits 15..4 "Audio Data"; bits 3..0 "Write - MCP's
   transmit logic automatically zero fills these bits". */
constexpr uint16_t kAudioDataMask = 0xFFF0u;

}

Sa11xxMcpAudioTransmitPath::Sa11xxMcpAudioTransmitPath(CerfEmulator& emu)
    : Sa11xxDmaTransmitFifo(emu, Geometry{kTxDepth, kRequestAt, kBurst, kAudioDataMask}) {}

REGISTER_SERVICE(Sa11xxMcpAudioTransmitPath);
