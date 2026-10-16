#define NOMINMAX
#include "imx51_gpu2d_stroker.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "imx51_id.h"
#include "imx51_gpu2d_rasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

REGISTER_SERVICE(Imx51Gpu2dStroker);

bool Imx51Gpu2dStroker::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx51;
}

namespace {

constexpr float kPi = 3.14159265358979323846f;

/* A round cap/join is a semicircle/wedge of radius = half the line width
   (openvg_spec.pdf p.102-103); a full disc's outer half IS that arc and its
   inner half overlaps the segment body (idempotent under the nonzero union). */
void AddDisc(std::vector<std::vector<float>>& out, float cx, float cy, float r, int n) {
    std::vector<float> c;
    c.reserve(static_cast<size_t>(n) * 2u);
    for (int i = 0; i < n; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
        c.push_back(cx + r * std::cos(a));
        c.push_back(cy + r * std::sin(a));
    }
    out.push_back(std::move(c));
}

float SignedArea2(const std::vector<float>& c) {
    float a = 0.0f;
    const size_t m = c.size() / 2u;
    for (size_t i = 0; i < m; ++i) {
        const size_t j = (i + 1u == m) ? 0u : i + 1u;
        a += c[i * 2u] * c[j * 2u + 1u] - c[j * 2u] * c[i * 2u + 1u];
    }
    return a;
}

/* g12 reg 0x66 = -cos(2*asin(1/miterLimit)) (sub_41C5B1B8), so spec 8.7.3's
   "miter length 1/sin(theta/2) > limit -> bevel" reduces to: bevel iff
   dot(u,w) < miter_thresh (trapezoid to the apex, else the bevel triangle). */
void AddMiterJoin(std::vector<std::vector<float>>& out, float ax, float ay,
                  float vx, float vy, float bx, float by, float r, float miter_thresh) {
    float ux = vx - ax, uy = vy - ay, wx = bx - vx, wy = by - vy;
    const float lu = std::sqrt(ux * ux + uy * uy), lw = std::sqrt(wx * wx + wy * wy);
    if (lu < 1e-6f || lw < 1e-6f) return;
    ux /= lu; uy /= lu; wx /= lw; wy /= lw;
    const float turn = ux * wy - uy * wx;          /* cross(u,w): >0 left turn, <0 right */
    if (std::fabs(turn) < 1e-6f) return;           /* collinear: no join gap */
    const float sgn = (turn >= 0.0f) ? 1.0f : -1.0f;  /* outer side of the turn */
    const float p1x = vx + sgn * uy * r, p1y = vy - sgn * ux * r;  /* outer end, incoming quad */
    const float p2x = vx + sgn * wy * r, p2y = vy - sgn * wx * r;  /* outer start, outgoing quad */
    if (ux * wx + uy * wy >= miter_thresh) {       /* within limit -> trapezoid to apex */
        const float t = ((p2x - p1x) * wy - (p2y - p1y) * wx) / turn;  /* P1+t*u == P2+s*w */
        out.push_back({vx, vy, p1x, p1y, p1x + t * ux, p1y + t * uy, p2x, p2y});
    } else {                                       /* miter length exceeds limit -> bevel */
        out.push_back({vx, vy, p1x, p1y, p2x, p2y});
    }
}

}  /* namespace */

/* Dilate the accumulated centerline into its stroke outline (spec 8.7.4) and
   coverage-fill it. Device half-width = radius_raw * |XF| * exp_scale, valid only
   for a uniform unrotated XF (a sheared/anisotropic one strokes an ellipse). */
void Imx51Gpu2dStroker::Stroke(const Gpu2dFillTarget& t, float radius_raw,
                               float xf_a, float xf_b, float xf_c, float xf_d,
                               float exp_scale, uint32_t cap, uint32_t join,
                               float miter_thresh) {
    auto& rast = emu_.Get<Imx51Gpu2dRasterizer>();
    /* Modeled: cap BUTT(0)/ROUND(1), join MITER(0)/ROUND(1). SQUARE cap(2) and
       BEVEL join(2) stay born-FATAL. vgenums_z160.h V2_CAP/V2_JOIN. */
    if (cap > 1u || join > 1u)
        rast.HaltFill("stroke cap/join style (BUTT/ROUND cap, MITER/ROUND join modeled)",
                      static_cast<float>(cap), static_cast<float>(join));
    /* Device half-width in x and y; they differ by more than the g12's 1/16-px
       coverage step (VGSPAN.COVERAGE[4], S205) only for a genuine elliptical pen.
       A sub-step difference is IEEE rounding in the guest's uniform-scale matrix
       (e.g. 0.400024 vs 0.399933) -> stroke a circle at the mean radius. */
    const float rx = radius_raw * std::fabs(xf_a) * exp_scale;
    const float ry = radius_raw * std::fabs(xf_d) * exp_scale;
    if (xf_b != 0.0f || xf_c != 0.0f || std::fabs(rx - ry) > 1.0f / 16.0f) {
        LOG(Caution, "[GPU2D-STROKE] rotated/anisotropic XF a=%.6f b=%.6f c=%.6f d=%.6f rx=%.4f ry=%.4f\n",
            xf_a, xf_b, xf_c, xf_d, rx, ry);
        rast.HaltFill("stroke under rotated/anisotropic transform", xf_b, xf_c);
    }
    const float radius = 0.5f * (rx + ry);
    if (radius <= 0.0f) rast.HaltFill("stroke non-positive radius", radius, 0.0f);

    const std::vector<float>&    pts    = rast.PathPoints();
    const std::vector<uint32_t>& starts = rast.PathStarts();
    const std::vector<uint8_t>&  closed = rast.PathClosed();
    const uint32_t nsub = static_cast<uint32_t>(starts.size());
    const uint32_t npts = static_cast<uint32_t>(pts.size() / 2u);
    const int kArc = 16;  /* disc facets; sub-pixel-exact for the hairline radius */

    std::vector<std::vector<float>> contours;
    for (uint32_t s = 0; s < nsub; ++s) {
        const uint32_t first = starts[s];
        const uint32_t last  = (s + 1u < nsub) ? starts[s + 1u] : npts;

        std::vector<float> p;  /* subpath with zero-length segments removed (spec 8.7.4) */
        for (uint32_t e = first; e < last; ++e) {
            const float x = pts[e * 2u], y = pts[e * 2u + 1u];
            if (p.size() >= 2u && std::fabs(p[p.size() - 2u] - x) < 1e-4f &&
                std::fabs(p.back() - y) < 1e-4f)
                continue;
            p.push_back(x);
            p.push_back(y);
        }
        const uint32_t m = static_cast<uint32_t>(p.size() / 2u);
        if (m == 0u) continue;
        const bool sub_closed = closed[s] != 0u;

        /* Open-subpath endpoints are CAPS, every other vertex a JOIN. ROUND
           cap/join = a disc; BUTT cap = nothing; MITER join = the outer
           trapezoid/bevel (AddMiterJoin). Segment bodies = the offset quads below. */
        for (uint32_t i = 0; i < m; ++i) {
            const float vx = p[i * 2u], vy = p[i * 2u + 1u];
            if (!sub_closed && (i == 0u || i + 1u == m)) {  /* end cap */
                if (cap == 1u) AddDisc(contours, vx, vy, radius, kArc);  /* ROUND; BUTT none */
                continue;
            }
            if (join == 1u) { AddDisc(contours, vx, vy, radius, kArc); continue; }  /* ROUND */
            const uint32_t pi = (i == 0u) ? m - 1u : i - 1u;   /* neighbors wrap when closed */
            const uint32_t ni = (i + 1u == m) ? 0u : i + 1u;
            AddMiterJoin(contours, p[pi * 2u], p[pi * 2u + 1u], vx, vy,
                         p[ni * 2u], p[ni * 2u + 1u], radius, miter_thresh);
        }
        const uint32_t nseg = (m == 1u) ? 0u : (sub_closed ? m : m - 1u);
        for (uint32_t i = 0; i < nseg; ++i) {
            const uint32_t a = i, b = (i + 1u == m) ? 0u : i + 1u;
            const float ax = p[a * 2u], ay = p[a * 2u + 1u];
            const float bx = p[b * 2u], by = p[b * 2u + 1u];
            const float dx = bx - ax, dy = by - ay;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-6f) continue;
            const float nx = dy / len * radius, ny = -dx / len * radius;  /* perpendicular*r */
            contours.push_back({ax + nx, ay + ny, bx + nx, by + ny,
                                bx - nx, by - ny, ax - nx, ay - ny});
        }
    }

    /* Normalize every contour to one winding so nonzero-inside unions overlaps
       (a piece wound the other way would subtract instead). */
    for (auto& c : contours)
        if (SignedArea2(c) < 0.0f) {
            const size_t m = c.size() / 2u;
            for (size_t i = 0; i < m / 2u; ++i) {
                std::swap(c[i * 2u], c[(m - 1u - i) * 2u]);
                std::swap(c[i * 2u + 1u], c[(m - 1u - i) * 2u + 1u]);
            }
        }

    uint32_t const_px = t.argb;
    bool per_pixel = false;
    if (t.blend) {
        if (!t.color_transform) {
            rast.ValidateBlendProg(t.prog_a);
            rast.ValidateBlendProg(t.prog_c);
        }
        per_pixel = t.color_transform || Imx51Gpu2dRasterizer::BlendProgRefsDest(t.prog_a) ||
                    Imx51Gpu2dRasterizer::BlendProgRefsDest(t.prog_c);
        if (!per_pixel) const_px = rast.BlendPixel(t, t.argb, 0u, 1.0f);
    }
    rast.FillContours(t, contours, const_px, per_pixel);
    rast.ClearPath();
}
