#include "mediaq_mq1188_ge.h"

#include <cerfcore/core/log.h>

const MediaQGe::Layout& MediaQMq1188Ge::Lyt() const {
    /* fg=GE07, bg=GE08, pat_fg=GE12, src_stride=GE09; GE0AR stride[9:0],
       GE0BR base[19:0] (Reg 4-93..4-98). */
    /* mono pattern GE10R/GE11R (16/17), PAT_FG GE12R (18), PAT_BG GE13R (19);
       color_pat_base=0 -> MQ-1132 has no colour-pattern array (GE ends at GE13R,
       datasheet Reg 4-104..4-107). */
    static const Layout kL{7u, 8u, 18u, 9u, 0x3FFu, 0xFFFFFu,
                           19u, 16u, 17u, 0u};
    return kL;
}

void MediaQMq1188Ge::BlitScreen(const uint32_t* r) {
    const uint32_t cmd = r[kGe00Command];
    if (IsSolidFill(cmd)) {
        FillSolid(static_cast<uint8_t>(cmd & kCmdRopMask), SolidFillColor(), cmd);
    } else if (cmd & kCmdMonoSrc) {
        BlitMonoFromDisplay(cmd);
    } else {
        BlitColorFromDisplay(cmd);
    }
}

void MediaQMq1188Ge::BlitSystem(const uint32_t* r) {
    if (r[kGe00Command] & kCmdMonoSrc) BlitMonoSource(r);
    else                               BlitColorSource(r);
}

void MediaQMq1188Ge::DrawLine(uint32_t cmd) {
    DrawBresenhamLine(cmd, LineColor(cmd));
}

bool MediaQMq1188Ge::IsSolidFill(uint32_t cmd) const {
    return (cmd & kCmdSolidPat) != 0u;
}

uint32_t MediaQMq1188Ge::SolidFillColor() const {
    return reg_[kGe12PatFg];
}

uint32_t MediaQMq1188Ge::LineColor(uint32_t cmd) const {
    /* GDI pen lines are a solid source (GE07) or solid pattern (GE12); a line
       textured from the Source FIFO / pattern bitmap never occurs. */
    if (cmd & kCmdSolidSrc) return reg_[kGe07FgColor];
    if (cmd & kCmdSolidPat) return reg_[kGe12PatFg];
    LOG(Caution, "MediaQ MQ1188 GE: non-solid line source (cmd=0x%08X)\n", cmd);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

/* Source dwords the latched system-source blit streams: contiguous 1-bpp bits
   for a mono source (sub_184550C), or 16-bpp pixels two-per-dword for a colour
   source (sub_1845A9C), first pixel at GE09R bit 4. */
uint32_t MediaQMq1188Ge::ExpectedSourceDwords() const {
    const uint32_t w = reg_[kGe01Size] & 0xFFFu;
    const uint32_t h = (reg_[kGe01Size] >> 16) & 0xFFFu;
    if (w == 0u || h == 0u) return 0u;
    if (reg_[kGe00Command] & kCmdMonoSrc) {
        const uint32_t g09 = reg_[kGe09SrcStride];
        const uint32_t bit_off0 = g09 & 0x1Fu;
        const uint32_t stride_bits = w + (g09 >> 25);
        const uint32_t total_bits = bit_off0 + (h - 1u) * stride_bits + w;
        return (total_bits + 31u) / 32u;
    }
    const uint32_t start_px = (reg_[kGe09SrcStride] >> 4) & 1u;
    const uint32_t dwords_per_row = (start_px + ((start_px + w) & 1u) + w) / 2u;
    return dwords_per_row * h;
}
