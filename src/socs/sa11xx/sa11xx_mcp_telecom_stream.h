#pragma once

#include "sa11xx_mcp_channel.h"

namespace cerfcore {

class Sa11xxMcpTelecomStream : public Sa11xxMcpChannel {
public:
    explicit Sa11xxMcpTelecomStream(CerfEmulator& emu);

    void OnReady() override;
};

}
