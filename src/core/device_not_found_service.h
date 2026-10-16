#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class DeviceNotFoundService : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void EnsureFound();

private:
    bool IsDevicePresent();
};

}
