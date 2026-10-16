#include "sa11xx_mcp_telecom_transmit_path.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_mcp_regs.h"

namespace cerfcore {

using namespace sa11xx_mcp;

namespace {

/* SA-1110 §11.12.5.2 MCDR1 (printed 11-143): bits 15..2 "Telecom Data"; bits 1..0 "Write - MCP's
   transmit logic automatically zero fills these bits". */
constexpr uint16_t kTelecomDataMask = 0xFFFCu;

}

Sa11xxMcpTelecomTransmitPath::Sa11xxMcpTelecomTransmitPath(CerfEmulator& emu)
    : Sa11xxDmaTransmitFifo(emu, Geometry{kTxDepth, kRequestAt, kBurst, kTelecomDataMask}) {}

REGISTER_SERVICE(Sa11xxMcpTelecomTransmitPath);

}
