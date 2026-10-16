#pragma once

#include <cerfcore/core/service.h>

class NoEmulationRuntimeService : public Service {
public:
    using Service::Service;

    void OnReady() override;

    void EnsureEmulationPrevented();

private:
    bool checked_ = false;
};
