#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_system_host.h>

class MipsCp0Model : public Service {
public:
    using Service::Service;

    virtual cerfengine::MipsCp0Decision Decide(uint32_t rd) = 0;

protected:
    static bool RegWritable(uint32_t rd) {
        return rd != cerfengine::MipsCp0::kPRId && rd != cerfengine::MipsCp0::kRandom &&
               rd != cerfengine::MipsCp0::kBadVAddr;
    }
};
