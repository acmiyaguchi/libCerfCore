#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

struct Gpu2dFillTarget;

class Imx51Gpu2dPixelBlend : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void        ValidateBlendProg(uint32_t prog) const;
    static bool BlendProgRefsDest(uint32_t prog);
    uint32_t    BlendPixel(const Gpu2dFillTarget& t, uint32_t src, uint32_t dst, float cov) const;

private:
    [[noreturn]] void HaltBlend(const char* why, uint32_t prog) const;
};

}
