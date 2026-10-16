#pragma once

#include "service.h"

#include <cstdint>

class UserConfigWriter : public Service {
public:
    using Service::Service;

    void WriteConfigurableScreenSize(uint32_t width, uint32_t height);
};
