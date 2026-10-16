#pragma once

#include <cerfcore/core/service.h>

class Pr31x00SibIrqPin : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void OnPinEdge(bool rising);
};
