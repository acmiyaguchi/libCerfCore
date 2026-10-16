#pragma once

#include "../core/service.h"

#include <string>

namespace cerfcore {

class DeviceSelection : public Service {
public:
    using Service::Service;

    virtual std::string SelectedDevice() = 0;
};

}
