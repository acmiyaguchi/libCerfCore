#include "mips_tlb_model.h"

#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

uint32_t MipsTlbModel::NextRandom(uint32_t first, uint32_t nb) {
    if (nb == 1) {
        return first;
    }
    uint32_t idx;
    do {
        lcg_seed_ = 1103515245u * lcg_seed_ + 12345u;
        idx = (lcg_seed_ >> 16) % nb + first;
    } while (idx == prev_random_idx_);
    prev_random_idx_ = idx;
    return idx;
}

void MipsTlbModel::FlushAll(cerfengine::MipsTlbContext& ctx) {
    ctx.InvalidateAddressSpace();
    cerfengine::MipsCpuState* st = ctx.State();
    st->tlb_in_use = st->nb_tlb;
}

void MipsTlbModel::SaveState(StateWriter& w) const {
    w.Write("lcg_seed", lcg_seed_);
    w.Write("prev_random_idx", prev_random_idx_);
}

void MipsTlbModel::RestoreState(StateReader& r) {
    r.Read("lcg_seed", lcg_seed_);
    r.Read("prev_random_idx", prev_random_idx_);
}

}
