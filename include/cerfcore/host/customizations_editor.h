#pragma once

#include "../core/service.h"

class CustomizationsEditor : public Service {
public:
    using Service::Service;

    virtual void Open() = 0;
};
