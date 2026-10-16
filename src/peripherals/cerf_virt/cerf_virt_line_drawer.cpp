#include "cerf_virt_line_drawer.h"
#include "cerf_virt_blt_pixelops.h"
#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>

namespace CerfVirt {

REGISTER_SERVICE(CerfVirtLineDrawer);

bool CerfVirtLineDrawer::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtLineDrawer::OnReady() {
    fb_  = &emu_.Get<CerfVirtFramebuffer>();
    arena_  = &emu_.Get<CerfVirtDmaArena>();
}

bool CerfVirtLineDrawer::Execute(const CerfLineDescriptor& l) {
    if (l.magic != kCerfLineMagic) {
        LOG(Cerf, "[CerfVirtLineDrawer] corrupt line magic 0x%08X\n", l.magic);
        CerfFatalExit();
    }
    if (l.i_dir < 0 || l.i_dir > 7) {
        LOG(Cerf, "[CerfVirtLineDrawer] corrupt iDir %d\n", l.i_dir);
        CerfFatalExit();
    }
    const uint32_t bits = BltPixelOps::FormatBits(l.dst.format);
    if (bits == 0u) {
        LOG(Cerf, "[CerfVirtLineDrawer] UNIMPLEMENTED dst format %u (%u bpp)\n",
            l.dst.format, bits);
        CerfFatalExit();
    }
    const bool     subbyte = bits < 8u;
    const uint32_t bpp     = subbyte ? 1u : bits / 8u;
    Surface dst;
    if (!ResolveSurface(l.dst, bpp, &dst)) {
        LOG(Cerf, "[CerfVirtLineDrawer] dst surface unresolvable: buffer=0x%08X "
                  "is_fb_pa=%u fmt=%u stride=%d stage_off=0x%08X stage_len=%u\n",
            l.dst.buffer, l.dst.is_fb_pa, l.dst.format, l.dst.stride,
            l.dst.stage_off, l.dst.stage_len);
        CerfFatalExit();
    }

    const uint32_t base_mask = (bits >= 32u) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    const uint32_t pen       = l.solid_color & base_mask;
    const uint8_t  rop2_mark  = (uint8_t)l.mix;
    const uint8_t  rop2_space = (uint8_t)(l.mix >> 8);

    static const int8_t kDir[8][4] = {
        {  1,  0,  0,  1 }, {  0,  1,  1,  0 }, {  0,  1, -1,  0 }, { -1,  0,  0,  1 },
        { -1,  0,  0, -1 }, {  0, -1, -1,  0 }, {  0, -1,  1,  0 }, {  1,  0,  0, -1 },
    };
    const int8_t* dir = kDir[l.i_dir];

    long accum = (long)l.d_n + l.ll_gamma;
    const long axstp = (long)l.d_n;
    const long dgstp = (long)l.d_n - (long)l.d_m;

    int32_t x = l.x_start, y = l.y_start;
    int32_t style_state = l.style_state;
    for (int32_t n = l.c_pels; n > 0; --n) {
        const uint8_t rop2 = ((l.style >> ((uint32_t)(style_state++) & 31u)) & 1u)
                           ? rop2_space : rop2_mark;
        const bool clipped_out = l.has_clip != 0u &&
            (x < l.clip_rect.left || x >= l.clip_rect.right ||
             y < l.clip_rect.top  || y >= l.clip_rect.bottom);
        if (!clipped_out &&
            (l.band_y_count == 0u ||
             ((uint32_t)y >= l.band_y_first &&
              (uint32_t)y < l.band_y_first + l.band_y_count))) {
            if (subbyte) {
                uint32_t d = 0;
                if (!ReadSubBytePixel(dst, x, y, bits, &d)) {
                    LOG(Cerf, "[CerfVirtLineDrawer] dst pixel unaddressable at x=%d y=%d: "
                              "buffer=0x%08X is_fb_pa=%u stage_off=0x%08X stage_len=%u\n",
                        x, y, l.dst.buffer, l.dst.is_fb_pa, l.dst.stage_off, l.dst.stage_len);
                    CerfFatalExit();
                }
                const uint32_t v = BltPixelOps::ApplyRop2(rop2, pen, d) & base_mask;
                if (!WriteSubBytePixel(dst, x, y, bits, v)) CerfFatalExit();
            } else {
                uint32_t run = 0;
                uint8_t* p = PixelPtr(dst, x, y, bpp, &run);
                if (!p) {
                    LOG(Cerf, "[CerfVirtLineDrawer] dst pixel unaddressable at x=%d y=%d: "
                              "buffer=0x%08X is_fb_pa=%u stage_off=0x%08X stage_len=%u\n",
                        x, y, l.dst.buffer, l.dst.is_fb_pa, l.dst.stage_off, l.dst.stage_len);
                    CerfFatalExit();
                }
                const uint32_t d = (run >= bpp) ? BltPixelOps::ReadPixel(p, bpp)
                                                : ReadStraddlePixel(dst, x, y, bpp);
                const uint32_t v = BltPixelOps::ApplyRop2(rop2, pen, d) & base_mask;
                if (run >= bpp) BltPixelOps::WritePixel(p, bpp, v);
                else            WriteStraddlePixel(dst, x, y, bpp, v);
            }
        }
        if (n == 1) break;
        x += dir[0]; y += dir[1];
        if (axstp) {
            if (accum < 0) accum += axstp;
            else { x += dir[2]; y += dir[3]; accum += dgstp; }
        }
    }
    fb_->MarkDirty();
    return true;
}

}
