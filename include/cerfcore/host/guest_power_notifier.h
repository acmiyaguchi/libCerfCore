#pragma once

#include "../core/service.h"

#include <functional>
#include <vector>

namespace cerfcore {

enum class ResumeSource { User, Hardware };

class GuestPowerNotifier : public Service {
public:
    using Service::Service;

    void NotifyPowerDown();
    void NotifyReboot();
    void NotifyResume(ResumeSource src);
    void NotifyHardReset();

    void RegisterRebootListener(std::function<void()> fn);

private:
    void Banner(const char* line);

    std::vector<std::function<void()>> reboot_listeners_;
};

}
