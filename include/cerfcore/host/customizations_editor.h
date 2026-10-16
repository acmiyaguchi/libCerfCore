#pragma once

#include "../core/service.h"

namespace cerfcore {

class CustomizationsEditor : public Service {
public:
    using Service::Service;

    virtual void Open() = 0;
};

}
