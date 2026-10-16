#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class Mc13783IntLine : public Service {
public:
    using Service::Service;

    virtual void SetMc13783IntAsserted(bool asserted) = 0;
};

}
