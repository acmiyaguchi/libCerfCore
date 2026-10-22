#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 22.8.4 HW_RTC_SECONDS: the analog seconds register resets to zero only
   at power-on reset. */
class Imx28RtcSecondsSeed : public Service {
public:
    using Service::Service;

    virtual uint32_t SecondsAtHostNow() = 0;
};

}
