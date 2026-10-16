#pragma once

#include "../mediaq_ge/mediaq_ge.h"

#include <cstdint>

/* MQ-200 (SIMpad SL4) instance of the shared MediaQ 2D engine: MQ-200 register
   indices, solid-fill encoding, and Source-FIFO packing only (ddi.dll
   sub_13458A0 fill, sub_1346EF8 colour source). */
class MediaQMq200Ge : public MediaQGe {
public:
    using MediaQGe::MediaQGe;

    /* CC01R Source/Command FIFO + GE status (Data Book Table 5-86): CFS[4:0]=
       0x10 command FIFO empty, SFS[11:8]=0x8 source FIFO empty, bit16 busy=0. */
    uint32_t StatusReady() const override { return 0x10u | (0x8u << 8); }

    const char* CommandUnmodeled(uint32_t cmd) const;

protected:
    const Layout& Lyt() const override;
    uint32_t      ExpectedSourceDwords() const override;
    void          BlitScreen(const uint32_t* r) override;
    void          BlitSystem(const uint32_t* r) override;
    void          DrawLine(uint32_t cmd) override;
    bool          AutoExecute() const override;

private:
    class Pixel;

    enum : uint32_t {
        kGe07FgColor  = 0x07,
        kGe08BgColor  = 0x08,
        kGe09SrcOff   = 0x09,
    };

    void Blit(const uint32_t* r, bool system_source);
    uint32_t MonoSourceBit(const uint32_t* r, uint32_t col, uint32_t row, bool& present) const;
    uint32_t ColorSourcePixel(const uint32_t* r, uint32_t col, uint32_t row, bool& present) const;
};
