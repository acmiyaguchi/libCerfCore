#pragma once

#include <cerfcore/core/service.h>

class BoardNotFoundService : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void EnsureFound();
};
