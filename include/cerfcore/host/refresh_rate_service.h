#pragma once

#include "../core/service.h"

namespace cerfcore {

class RefreshRateService : public Service {
public:
    using Service::Service;

    int GetRefreshRate();
    int FrameIntervalMs();

private:
    int Decide();

    int cached_ = 0;
};

}
