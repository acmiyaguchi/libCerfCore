#pragma once

#include "../core/service.h"

class KeyboardMappingView : public Service {
public:
    using Service::Service;

    virtual void Show() = 0;
};
