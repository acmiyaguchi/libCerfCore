#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>
#include <cerfengine/mips/mips_system_host.h>
#include <cerfengine/mips/mips_tlb_context.h>

namespace cerfengine { struct MipsCpuState; }

namespace cerfcore {

class StateWriter;
class StateReader;

class MipsTlbModel : public Service {
public:
    using Service::Service;

    virtual cerfengine::MipsTlbHooks Hooks()                     = 0;
    virtual uint32_t RandomIndex(cerfengine::MipsCpuState* st) = 0;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

protected:
    uint32_t NextRandom(uint32_t first, uint32_t nb);

    static void FlushAll(cerfengine::MipsTlbContext& ctx);

private:
    uint32_t lcg_seed_        = 1;
    uint32_t prev_random_idx_ = 0;
};

}
