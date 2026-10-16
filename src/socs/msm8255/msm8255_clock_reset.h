#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>
#include <vector>

class Msm8255ClockReset : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void RegisterListener(uint32_t clock, std::function<void()> fn);
    void Reset(uint32_t clock);

private:
    struct Listener {
        uint32_t              clock;
        std::function<void()> fn;
    };

    std::vector<Listener> listeners_;
};
