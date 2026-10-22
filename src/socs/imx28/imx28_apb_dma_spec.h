#pragma once

#include <cstdint>

namespace cerfcore {

constexpr uint32_t kImx28ApbDmaChannels = 16u;
constexpr uint32_t kImx28ApbDmaNoSource = 0xFFFFFFFFu;

/* MCIMX28RM Rev. 2 Table 6-1, Table 7-1, Table 5-1 and the register tables of sections 6.5, 7.5. */
struct Imx28ApbDmaSpec {
    struct Channel {
        const char* usage;
        uint32_t    irq_source;
    };

    uint32_t ctrl0_reset;
    uint32_t ctrl0_writable;
    uint32_t clkgate_channel_mask;
    uint32_t version;
    uint32_t cmd_reserved_mask;
    uint32_t nand_channels;
    bool     has_hot;
    bool     has_burst_and_debug;
    Channel  channel[kImx28ApbDmaChannels];
};

}
