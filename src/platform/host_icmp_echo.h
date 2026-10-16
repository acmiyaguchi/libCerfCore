#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace host_platform {

enum class IcmpEchoStatus { kOpenFailed, kNoReply, kFailed, kReply };

struct IcmpEchoResult {
    IcmpEchoStatus       status = IcmpEchoStatus::kOpenFailed;
    uint32_t             code   = 0;
    std::vector<uint8_t> data;
};

IcmpEchoResult SendIcmpEcho(uint32_t dest_ip_n, const uint8_t* payload, size_t size,
                            uint32_t timeout_ms);

}
