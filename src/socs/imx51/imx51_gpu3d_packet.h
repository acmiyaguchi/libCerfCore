#pragma once

#include <cstdint>

enum class Imx51Gpu3dPacketSource { Ring, IndirectBuffer };

struct Imx51Gpu3dPacket {
    uint64_t address = 0;
    uint32_t header = 0;
    uint32_t type = 0;
    uint32_t opcode = 0;
    uint32_t payload_count = 0;
    bool same_register = false;
    bool predicate = false;
    Imx51Gpu3dPacketSource source = Imx51Gpu3dPacketSource::Ring;

    static const char* Decode(uint32_t header, uint64_t address, uint32_t available_dwords,
                              Imx51Gpu3dPacketSource source, Imx51Gpu3dPacket& packet);
    bool OperandAddress(uint32_t index, uint64_t& address) const;
};
