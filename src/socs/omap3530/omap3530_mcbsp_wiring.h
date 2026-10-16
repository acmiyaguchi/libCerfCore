#pragma once

#include <cerfcore/core/service.h>
#include "omap3530_mcbsp_link.h"

#include <cstdint>

class Omap3530McBspWiring : public Service {
public:
    using Service::Service;

    virtual Omap3530McBspPeer*     Peer(uint32_t port) = 0;
    virtual Omap3530McBspClockPad* ClksPad() = 0;
};
