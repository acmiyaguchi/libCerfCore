#pragma once

#include "../core/service.h"

class TaskManagerView : public Service {
public:
    using Service::Service;

    virtual void Show() = 0;
};
