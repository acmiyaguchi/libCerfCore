#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class NoEmulationRuntimeService : public Service {
public:
    using Service::Service;

    void OnReady() override;

    void EnsureEmulationPrevented();

private:
    bool checked_ = false;
};

}
