#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

/* MCIMX31RM section 37.5.7 (page 37-11): "WDOG is applied to the WDOG pin." */
class Imx31WdogPin : public Service {
public:
    using Service::Service;

    virtual void SetWdogAsserted(bool asserted) = 0;
};

}
