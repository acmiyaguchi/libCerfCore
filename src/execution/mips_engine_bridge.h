#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <cerfengine/mips/mips_system_host.h>
#include "engine_bridge.h"

class MipsEngineBridge : public EngineBridge, public cerfengine::MipsSystemHost {
public:
    using EngineBridge::EngineBridge;

    bool ShouldRegister() override;

    void RegisterResetListener(std::function<void()> fn);
    void RegisterPostResetListener(std::function<void()> fn);
    void RegisterRestoreListener(std::function<void()> fn);

    void SaveCpuState(StateWriter& w) override;
    void RestoreCpuState(StateReader& r) override;
    void SaveMmuState(StateWriter& w) override;
    void RestoreMmuState(StateReader& r) override;
    void DispatchTraceIter() override;
    void MapInjectionBand(uint32_t va, uint32_t pa, uint32_t size) override;

    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd) override;
    void                        MipsReset(cerfengine::MipsCpuState* st) override;

protected:
    void FillSetup(cerfengine::Setup& setup) override;
    void ColdBoot() override;

private:
    std::vector<std::function<void()>> reset_listeners_;
    std::vector<std::function<void()>> post_reset_listeners_;
    std::vector<std::function<void()>> restore_listeners_;
};
