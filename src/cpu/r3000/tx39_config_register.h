#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <cerfcore/core/service.h>
#include <cerfengine/calling_convention.h>

namespace cerfengine { struct MipsCpuState; }

namespace cerfengine { class Engine; }

class Tx39ConfigRegister : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t ReducedFrequency() const;
    void     RegisterReducedFrequencyListener(std::function<void()> fn);

    static void CERFENGINE_FASTCALL WriteConfig(void* reg, uint32_t value);

private:
    void ApplyReset();

    cerfengine::Engine*                engine_    = nullptr;
    cerfengine::MipsCpuState*                      cpu_state_ = nullptr;
    std::vector<std::function<void()>> rf_listeners_;
};
