#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class Pm8058IrqLine : public Service {
public:
    using Service::Service;

    virtual void SetPm8058IrqAsserted(bool asserted) = 0;
};

}
