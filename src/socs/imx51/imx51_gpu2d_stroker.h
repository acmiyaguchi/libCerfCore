#pragma once

#include <cerfcore/core/service.h>
#include "imx51_gpu2d_rasterizer.h"

#include <cstdint>

/* Dilates the rasterizer's accumulated stroke centerline into an outline
   (segment offset-quads unioned per-vertex with the cap/join geometry) and
   coverage-fills it via the rasterizer. Modeled under an isotropic transform:
   BUTT/ROUND caps, MITER (with bevel fallback) / ROUND joins; other styles halt. */
class Imx51Gpu2dStroker : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* Stroke the rasterizer's accumulated centerline: device half-width =
       radius_raw * |XF| * exp_scale (isotropic transform only, else halt),
       cap/join geometry, coverage-fill the outline, clear the path.
       miter_thresh = g12 reg 0x66 (the miter-limit test bound, join==MITER only). */
    void Stroke(const Gpu2dFillTarget& t, float radius_raw, float xf_a, float xf_b,
                float xf_c, float xf_d, float exp_scale, uint32_t cap, uint32_t join,
                float miter_thresh);
};
