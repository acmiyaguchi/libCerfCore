#pragma once
#include <cerfcore/core/service.h>
#include <cerfcore/core/device_config.h>

class ConfigLoader : public Service {
public:
    using Service::Service;

    void LoadInto(DeviceConfig& config);

    void ApplyBoardPanelDefault(DeviceConfig& config);
};
