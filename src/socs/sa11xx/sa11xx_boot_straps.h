#pragma once

#include <cerfcore/core/service.h>

class Sa11xxBootStraps : public Service {
public:
    using Service::Service;

    virtual bool RomSelHigh() const = 0;
    virtual bool SmromEnHigh() const = 0;
};
