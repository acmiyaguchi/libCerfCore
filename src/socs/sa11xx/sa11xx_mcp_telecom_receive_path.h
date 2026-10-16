#pragma once

#include "sa11xx_mcp_receive_path.h"

class Sa11xxMcpTelecomReceivePath : public Sa11xxMcpReceivePath {
public:
    explicit Sa11xxMcpTelecomReceivePath(CerfEmulator& emu);
};
