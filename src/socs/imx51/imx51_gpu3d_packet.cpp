#include "imx51_gpu3d_packet.h"
#include "imx51_gpu3d_regs.h"

/* NXP linux-imx a1638da9d9fda588979360b33390cf612a256829,
   drivers/mxc/amd-gpu/include/api/gsl_pm4types.h: packet macros and header types. */
const char* Imx51Gpu3dPacket::Decode(uint32_t header, uint64_t address,
                                    uint32_t available_dwords,
                                    Imx51Gpu3dPacketSource source,
                                    Imx51Gpu3dPacket& packet) {
    using namespace imx51_gpu3d_regs;
    packet = {};
    packet.address = address;
    packet.header = header;
    packet.source = source;
    packet.type = header >> 30;
    if (available_dwords == 0 || address > UINT32_MAX || (address & 3u) != 0)
        return "PM4 malformed header address/extent";
    if (packet.type == 1u)
        return "PM4 unsupported TYPE1";
    if (packet.type == kPm4Type2)
        return nullptr;
    packet.payload_count = ((header >> 16) & 0x3FFFu) + 1u;
    packet.same_register = packet.type == kPm4Type0 && (header & 0x8000u) != 0;
    if (packet.type == kPm4Type3) {
        packet.opcode = (header >> 8) & 0xFFu;
        packet.predicate = (header & 1u) != 0;
        if (packet.predicate)
            return "PM4 unsupported predicate";
        if ((header & 0x80FEu) != 0)
            return "PM4 unsupported reserved header flags";
        /* NXP linux-imx a1638da9, gsl_drawctxt.c:1237, shader partition fixup. */
        if (packet.opcode == kPm4OpRegRmw && packet.payload_count < 3u)
            return "PM4 malformed REG_RMW payload";
        /* sync_2 EA5T-14D544-BA.sec, lib2d-z430.dll: 0x41A62890, literal 0x41A62900. */
        if (packet.opcode == kPm4OpWaitRegEq && packet.payload_count < 4u)
            return "PM4 malformed WAIT_REG_EQ payload";
    }
    if (address + uint64_t(packet.payload_count) * 4u > UINT32_MAX)
        return "PM4 malformed packet address overflow";
    /* NXP linux-imx a1638da9d9fda588979360b33390cf612a256829,
       drivers/mxc/amd-gpu/common/gsl_ringbuffer.c: kgsl_ringbuffer_waitspace. */
    const bool ring_nop = source == Imx51Gpu3dPacketSource::Ring &&
                         packet.type == kPm4Type3 && packet.opcode == kPm4OpNop;
    if (packet.payload_count >= available_dwords && !ring_nop)
        return "PM4 malformed truncated payload";
    return nullptr;
}

bool Imx51Gpu3dPacket::OperandAddress(uint32_t index, uint64_t& operand_address) const {
    if (index >= payload_count)
        return false;
    operand_address = address + (uint64_t(index) + 1u) * 4u;
    return operand_address <= UINT32_MAX;
}
