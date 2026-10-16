#pragma once

#include "../core/service.h"
#include "../state/shutdown_choice.h"

#include <functional>

namespace cerfcore {

class ShutdownHost : public Service {
public:
    using Service::Service;

    virtual void RunPromptJob(std::function<void()> job) = 0;
    virtual ShutdownChoice Ask(ShutdownTrigger trigger) = 0;
    virtual void DismissAsCancel() = 0;

    virtual void BeginTeardown() = 0;
    virtual void CancelTeardown() = 0;
};

}
