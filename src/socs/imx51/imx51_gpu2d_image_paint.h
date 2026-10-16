#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

/* The g12 GRADW image-paint compositor (tiled-rect emitter sub_41C6338C): a rect
   fill whose per-pixel SOURCE is a GRADW texture sample, composited over BASE0 by
   the multi-pass blend program. Reads the command engine's register file. */
class Imx51Gpu2dImagePaint : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* One tile under G2D_INPUT 0x09 + GRADIENT.ENABLE2 + BLENDERCFG.ENABLE. */
    void Composite(const uint32_t (&regs)[0x100]);

private:
    void CheckGates(const uint32_t (&regs)[0x100]) const;
    /* Run PASSES+1 color + ALPHAPASSES+1 alpha micro-op passes over TEMP0-2 with
       operands SOURCE (the texel), IMAGE (identity; SRC1=0 disables the second
       texture) and DEST; returns TEMP0. */
    uint32_t BlendMultiPass(const uint32_t (&regs)[0x100], uint32_t src, uint32_t dst) const;
    [[noreturn]] void Halt(const uint32_t (&regs)[0x100], const char* why,
                           uint32_t addr, uint32_t data) const;
};
