#pragma once

#include "service.h"

#include <functional>
#include <vector>

namespace cerfcore {

class DeviceConfigRefresh : public Service {
public:
    using Service::Service;

    void RegisterListener(std::function<void()> fn);

    void Refresh();

private:
    std::vector<std::function<void()>> listeners_;
};

}
