#pragma once

#include <cerfcore/core/service.h>
#include <cerfengine/calling_convention.h>
#include <cerfengine/engine_setup.h>
#include <cerfengine/mips/mips_system_host.h>

namespace cerfengine { struct MipsCpuState; }

namespace cerfcore {

class MipsProcessorConfig;

class MipsExceptionModel : public Service {
public:
    using Service::Service;

    void OnReady() override;

    virtual cerfengine::MipsExceptionHooks Hooks() = 0;

protected:
    static void CERFENGINE_FASTCALL EretHook(void* model, cerfengine::MipsCpuState* st);
    static void CERFENGINE_FASTCALL RfeHook(void* model, cerfengine::MipsCpuState* st);

private:
    MipsProcessorConfig* config_ = nullptr;
};

}
