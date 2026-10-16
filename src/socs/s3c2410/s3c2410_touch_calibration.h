#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class S3C2410TouchCalibration : public Service {
public:
    using Service::Service;

    virtual void MapHostToSample(int host_x, int host_y,
                                 double screen_w, double screen_h,
                                 uint16_t& sample_x, uint16_t& sample_y) const = 0;

    virtual bool AxisSwap() const = 0;

protected:
    /* S3C2410A User Manual pp. 16-10/16-11: ADCDAT0 XPDATA and ADCDAT1 YPDATA
       are [9:0]. */
    static uint16_t ClampSample(double v) {
        if (v <    0.0) return 0;
        if (v > 1023.0) return 1023;
        return (uint16_t)(v + 0.5);
    }
};

}
