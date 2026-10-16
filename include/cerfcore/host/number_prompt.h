#pragma once

#include "../core/service.h"

#include <cstdint>
#include <string>

namespace cerfcore {

class NumberPrompt : public Service {
public:
    using Service::Service;

    virtual uint32_t AskAtLeast(const std::string& title, const std::string& caption,
                                uint32_t minimum) = 0;
};

}
