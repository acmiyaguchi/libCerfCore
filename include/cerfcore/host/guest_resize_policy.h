#pragma once

#include "../core/service.h"

class GuestResizePolicy : public Service {
public:
    using Service::Service;

    virtual bool Available() const = 0;
    virtual bool Enabled() const = 0;
    virtual void SetEnabled(bool on) = 0;
};
