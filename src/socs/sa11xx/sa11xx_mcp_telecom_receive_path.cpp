#include "sa11xx_mcp_telecom_receive_path.h"

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

Sa11xxMcpTelecomReceivePath::Sa11xxMcpTelecomReceivePath(CerfEmulator& emu)
    : Sa11xxMcpReceivePath(emu) {}

REGISTER_SERVICE(Sa11xxMcpTelecomReceivePath);

}
