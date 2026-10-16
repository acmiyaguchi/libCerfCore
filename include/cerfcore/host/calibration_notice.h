#pragma once

#include "../core/service.h"

class CalibrationNotice : public Service {
public:
    using Service::Service;

    virtual void OnCalibrationAppeared() = 0;
    virtual void OnCalibrationDisappeared() = 0;
};
