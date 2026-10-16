#include "imx51_ipu_cpmem.h"

#include <cstdint>

/* CPMEM bitfield positions (word, bit-offset, width) + the value-1 / EBA0<<3
   encodings are the IPUv3 layout from Linux drivers/gpu/ipu-v3/ipu-cpmem.c
   (IPU_FIELD_* macros) + MCIMX51RM Ch 42. */
uint32_t Imx51IpuCpmem::Field(uint32_t ch, uint32_t word, uint32_t off, uint32_t len) const {
    const uint32_t base = ch * 16u + word * 8u;   /* 64 B/channel, 32 B/word */
    const uint32_t i = base + (off >> 5);
    const uint32_t bit = off & 31u;
    uint64_t v = static_cast<uint64_t>(regs_[i]) >> bit;
    if (bit + len > 32u)
        v |= static_cast<uint64_t>(regs_[i + 1]) << (32u - bit);
    return static_cast<uint32_t>(v & ((1ull << len) - 1ull));
}

Imx51IpuChannelDesc Imx51IpuCpmem::DecodeChannel(uint32_t ch) const {
    Imx51IpuChannelDesc d;
    d.eba = Field(ch, 1, 0, 29) << 3;     /* EBA0 -> byte address (RM W1[28:0]) */
    d.eba1 = Field(ch, 1, 29, 29) << 3;   /* EBA1 2nd double-buffer (RM p42-518, W1[57:29], addr/8) */
    d.fw  = Field(ch, 0, 125, 13) + 1u;   /* frame width  */
    d.fh  = Field(ch, 0, 138, 12) + 1u;   /* frame height */
    d.sl  = Field(ch, 1, 102, 14) + 1u;   /* stride bytes */
    d.bpp = Field(ch, 0, 107, 3);
    d.pfs = Field(ch, 1, 85, 4);
    d.alu = Field(ch, 1, 89, 1);          /* Alpha Used (RM p42-518, W1[89]) */
    d.valid = d.eba != 0u && d.fw > 1u && d.fh > 1u && d.fw <= 4096u && d.fh <= 4096u;
    return d;
}

REGISTER_SERVICE(Imx51IpuCpmem);
