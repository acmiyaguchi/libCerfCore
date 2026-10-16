#pragma once

#include "sa11xx_mcp_channel.h"

namespace cerfcore {

class Sa11xxMcpAudioStream : public Sa11xxMcpChannel {
public:
    explicit Sa11xxMcpAudioStream(CerfEmulator& emu);

    void OnReady() override;
};

}
