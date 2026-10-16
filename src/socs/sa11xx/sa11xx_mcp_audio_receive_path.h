#pragma once

#include "sa11xx_mcp_receive_path.h"

namespace cerfcore {

class Sa11xxMcpAudioReceivePath : public Sa11xxMcpReceivePath {
public:
    explicit Sa11xxMcpAudioReceivePath(CerfEmulator& emu);
};

}
