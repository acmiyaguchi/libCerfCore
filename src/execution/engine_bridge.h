#pragma once

#include <memory>

#include <cerfcore/core/service.h>
#include <cerfengine/engine.h>
#include <cerfengine/guest_engine.h>

class PageTableBuilder;
class RateProbe;
class StateReader;
class StateWriter;
class TraceManager;

class EngineBridge : public Service, public cerfengine::EngineHost {
public:
    using Service::Service;

    void OnReady() override;

    cerfengine::Engine&      Engine() { return *engine_; }
    cerfengine::GuestEngine& Guest() { return *guest_; }

    virtual void SaveCpuState(StateWriter& w)    = 0;
    virtual void RestoreCpuState(StateReader& r) = 0;
    virtual void SaveMmuState(StateWriter& w)    = 0;
    virtual void RestoreMmuState(StateReader& r) = 0;
    virtual void DispatchTraceIter()             = 0;
    virtual void MapInjectionBand(uint32_t va, uint32_t pa, uint32_t size) = 0;

    std::vector<cerfengine::DramRegion> CachedDramRegions() override;

    void OnResetRequested(bool is_resume) override;
    void OnRebootRequested() override;
    void OnResetDelivered() override;
    void OnPowerDownRequest() override;
    void OnBlockCompiled() override;

    bool HasPcTrace(uint32_t pc) override;
    void OnArmPcTrace(uint32_t pc, const uint32_t* gprs, uint32_t cpsr) override;
    void OnMipsPcTrace(uint32_t pc, const cerfengine::MipsCpuState* state) override;

    [[noreturn]] void Panic(const char* message) override;
    void Log(cerfengine::LogChannel channel, const char* text) override;
    bool Symbolize(const void* address, char* out, size_t out_size) override;

protected:
    virtual void FillSetup(cerfengine::Setup& setup) = 0;
    virtual void ColdBoot()                          = 0;

    TraceManager& Trace() { return *trace_; }

private:
    std::unique_ptr<cerfengine::Engine> engine_;
    cerfengine::GuestEngine*            guest_ = nullptr;

    PageTableBuilder* pages_ = nullptr;
    TraceManager*     trace_ = nullptr;
#if CERF_DEV_MODE
    RateProbe*        probe_ = nullptr;
#endif
};
