#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 22.8.2 STAT: XTAL32000_PRESENT [28] and XTAL32768_PRESENT [27]. */
class Imx28RtcCrystalFuses : public Service {
public:
    using Service::Service;

    virtual bool Xtal32768Present() = 0;
    virtual bool Xtal32000Present() = 0;
};

}
