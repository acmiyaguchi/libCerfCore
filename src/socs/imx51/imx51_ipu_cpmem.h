#pragma once

#include "imx51_ipu_internal_mem.h"

#include <cstdint>

/* Decoded IDMAC channel parameters (one CPMEM channel descriptor). */
struct Imx51IpuChannelDesc {
    uint32_t eba = 0;   /* framebuffer byte address (guest physical) = EBA0 << 3 */
    uint32_t eba1 = 0;  /* 2nd double-buffer address = EBA1 << 3 (RM p42-518 W1[57:29]) */
    uint32_t fw = 0;    /* frame width  (pixels) */
    uint32_t fh = 0;    /* frame height (pixels) */
    uint32_t sl = 0;    /* stride (bytes per line) */
    uint32_t bpp = 0;   /* BPP code: 0=32bpp 1=24bpp 3=16bpp 5=8bpp */
    uint32_t pfs = 0;   /* pixel format select */
    uint32_t alu = 0;   /* Alpha Used: 0=alpha in same buffer, 1=separate channel (RM p42-518 W1[89]) */
    bool valid = false;
};

/* CPMEM (IDMAC Channel Parameter Memory) @ 0x5F000000. The IPU display renderer
   resolves this (emu.Get<Imx51IpuCpmem>) to read the live scanout descriptor
   (EBA/FW/FH/format) for the active display channel. */
class Imx51IpuCpmem : public cerf_imx51_ipu_mem_detail::Imx51IpuInternalMem<0x5F000000u> {
public:
    using Imx51IpuInternalMem::Imx51IpuInternalMem;

    Imx51IpuChannelDesc DecodeChannel(uint32_t ch) const;

private:
    /* Read a CPMEM bitfield. Each channel descriptor is 64 bytes = 2 words of
       5x32 data + padding; field positions per the IPUv3 layout (Linux
       drivers/gpu/ipu-v3/ipu-cpmem.c IPU_FIELD_* macros + MCIMX51RM Ch 42). */
    uint32_t Field(uint32_t ch, uint32_t word, uint32_t off, uint32_t len) const;
};
