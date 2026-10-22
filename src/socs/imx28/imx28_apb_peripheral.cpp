#include "imx28_apb_peripheral.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

uint32_t Imx28ApbPeripheral::ReadLanes(uint32_t word_addr, uint32_t) { return ReadWord(word_addr); }

uint32_t Imx28ApbPeripheral::PeekLanes(uint32_t word_addr, uint32_t lanes) {
    return ReadLanes(word_addr, lanes);
}

uint8_t Imx28ApbPeripheral::ReadByte(uint32_t addr) {
    const uint32_t shift = (addr & 0x3u) * 8u;
    return static_cast<uint8_t>((ReadLanes(addr & ~0x3u, 0xFFu << shift) >> shift) & 0xFFu);
}

uint16_t Imx28ApbPeripheral::ReadHalf(uint32_t addr) {
    if ((addr & 0x1u) != 0) HaltUnsupportedAccess("ReadHalf-unaligned", addr, 0);
    const uint32_t shift = (addr & 0x2u) * 8u;
    return static_cast<uint16_t>((ReadLanes(addr & ~0x3u, 0xFFFFu << shift) >> shift) & 0xFFFFu);
}

/* MCIMX28RM Rev. 2 section 39.3. */
uint32_t Imx28ApbPeripheral::MergeNarrow(uint32_t addr, uint32_t shift, uint32_t mask,
                                         uint32_t value) {
    const uint32_t base = addr & ~0x3u;
    const bool     sct  = HasSct() && Imx28DecodeSct(base - MmioBase()).op != Imx28SctOp::Write;
    const uint32_t old  = sct ? 0u : PeekLanes(base, mask << shift);
    return (old & ~(mask << shift)) | (value << shift);
}

void Imx28ApbPeripheral::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t shift = (addr & 0x3u) * 8u;
    WriteWord(addr & ~0x3u, MergeNarrow(addr, shift, 0xFFu, value));
}

void Imx28ApbPeripheral::WriteHalf(uint32_t addr, uint16_t value) {
    if ((addr & 0x1u) != 0) HaltUnsupportedAccess("WriteHalf-unaligned", addr, value);
    const uint32_t shift = (addr & 0x2u) * 8u;
    WriteWord(addr & ~0x3u, MergeNarrow(addr, shift, 0xFFFFu, value));
}

bool Imx28ApbPeripheral::IsSoftReset(uint32_t old_ctrl, uint32_t next_ctrl) const {
    if ((next_ctrl & kCtrlSftrst) == 0u || (old_ctrl & kCtrlSftrst) != 0u) return false;
    if ((next_ctrl & kCtrlClkgate) != 0u && (old_ctrl & kCtrlClkgate) != 0u) {
        emu_.Get<Fatal>().Die("Imx28 %08X: SFTRST set with CLKGATE already set (CTRL %08X -> "
                              "%08X)", MmioBase(), old_ctrl, next_ctrl);
    }
    return (next_ctrl & kCtrlClkgate) == 0u;
}

void Imx28ApbPeripheral::RequireOperating(const char* op, uint32_t ctrl, uint32_t addr,
                                          uint32_t value) const {
    if (!Gated(ctrl)) return;
    emu_.Get<Fatal>().Die("Imx28 %08X: %s at %08X (value %08X) with CTRL %08X (SFTRST or CLKGATE "
                          "set)", MmioBase(), op, addr, value, ctrl);
}

}
