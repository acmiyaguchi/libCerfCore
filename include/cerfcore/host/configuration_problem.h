#pragma once

#include "../core/service.h"

#include <string>

namespace cerfcore {

class ConfigurationProblem : public Service {
public:
    using Service::Service;

    virtual void DeviceRomMissing(const std::string& device) = 0;
    virtual void BoardUnsupported(const std::string& board_id) = 0;
    virtual void LaunchCompactFlashMissing(const std::string& card_name) = 0;
};

}
