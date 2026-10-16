#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class Omap3530SysNirqPad : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void Drive(bool low);

private:
    void Route();

    bool low_ = false;
};

}
