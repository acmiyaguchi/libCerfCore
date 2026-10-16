#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

/* The g12 VGV2 path-fill dispatch: gates the register file for a modeled VG FLUSH,
   builds the fill target, and hands the accumulated path to the rasterizer (fill)
   or the stroker (stroke). Reads the command engine's register file. */
class Imx51Gpu2dVgFill : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* VGV2_ACTION=FLUSH: fill/stroke the accumulated path with the register file. */
    void Flush(const uint32_t (&regs)[0x100], bool bbox_live);

private:
    [[noreturn]] void Halt(const uint32_t (&regs)[0x100], const char* why,
                           uint32_t addr, uint32_t data) const;
};
