#pragma once

#include "../core/service.h"

namespace cerfcore {

class TaskManagerView : public Service {
public:
    using Service::Service;

    virtual void Show() = 0;
};

}
