#include "../mips/mips_cp0_model.h"

#include <cstddef>
#include <cstdint>

#include <cerfcore/boards/board_context.h>
#include "../../socs/vr4102/vr4102_id.h"
#include "../../socs/vr4111/vr4111_id.h"
#include "../../socs/vr4121/vr4121_id.h"
#include "../../socs/vr4122/vr4122_id.h"
#include "../vr5500/vr5500_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips/mips_cp0_ops.h"
#include "../mips_processor_config.h"

namespace cerfcore {

namespace {

int32_t RegOffset(uint32_t rd) {
    switch (rd) {
        case cerfengine::MipsCp0::kIndex:    return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_index));
        case cerfengine::MipsCp0::kRandom:   return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_random));
        case cerfengine::MipsCp0::kEntryLo0: return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_entrylo0));
        case cerfengine::MipsCp0::kEntryLo1: return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_entrylo1));
        case cerfengine::MipsCp0::kContext:  return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_context));
        case cerfengine::MipsCp0::kPageMask: return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_pagemask));
        case cerfengine::MipsCp0::kWired:    return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_wired));
        case cerfengine::MipsCp0::kBadVAddr: return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_badvaddr));
        case cerfengine::MipsCp0::kCount:    return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, count_save));
        case cerfengine::MipsCp0::kEntryHi:  return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_entryhi));
        case cerfengine::MipsCp0::kCompare:  return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_compare));
        case cerfengine::MipsCp0::kStatus:   return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_status));
        case cerfengine::MipsCp0::kCause:    return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_cause));
        case cerfengine::MipsCp0::kEPC:      return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_epc));
        case cerfengine::MipsCp0::kPRId:     return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_prid));
        case cerfengine::MipsCp0::kConfig:   return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_config));
        case cerfengine::MipsCp0::kWatchLo:  return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_watchlo));
        case cerfengine::MipsCp0::kWatchHi:  return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_watchhi));
        case cerfengine::MipsCp0::kTagLo:    return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_taglo));
        case cerfengine::MipsCp0::kTagHi:    return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_taghi));
        case cerfengine::MipsCp0::kErrorEPC: return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_errorepc));
        case cerfengine::MipsCp0::kLLAddr:   return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_lladdr));
        case cerfengine::MipsCp0::kXContext: return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_xcontext));
        case cerfengine::MipsCp0::kECC:      return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_ecc));
        default:                 return -1;
    }
}

class MipsCp0ModelR4000 : public MipsCp0Model {
public:
    using MipsCp0Model::MipsCp0Model;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Vr4102 || soc == SocId::Vr4111 || soc == SocId::Vr4121 ||
               soc == SocId::Vr4122 || soc == SocId::Vr5500;
    }

    void OnReady() override {
        config_ = &emu_.Get<MipsProcessorConfig>();
        ops_    = &emu_.Get<MipsCp0Ops>();
    }

    cerfengine::MipsCp0Decision Decide(uint32_t rd) override {
        cerfengine::MipsCp0Decision d;
        const int32_t off = RegOffset(rd);
        if (off < 0 || !config_->HasCp0Reg(rd)) {
            return d;
        }
        d.present  = true;
        d.offset   = off;
        d.writable = RegWritable(rd);
        if (rd == cerfengine::MipsCp0::kRandom) {
            d.read         = &MipsCp0Ops::ReadRandom;
            d.read_context = ops_;
        }
        if (rd == cerfengine::MipsCp0::kCount) {
            d.read          = &MipsCp0Ops::ReadCount;
            d.read_context  = ops_;
            d.write         = &MipsCp0Ops::WriteCount;
            d.write_context = ops_;
        }
        if (rd == cerfengine::MipsCp0::kCompare) {
            d.write         = &MipsCp0Ops::WriteCompare;
            d.write_context = ops_;
        }
        if (rd == cerfengine::MipsCp0::kEntryHi) {
            d.write         = &MipsCp0Ops::WriteEntryHiR4000;
            d.write_context = ops_;
        }
        return d;
    }

private:
    MipsProcessorConfig* config_ = nullptr;
    MipsCp0Ops*          ops_    = nullptr;
};

}

REGISTER_SERVICE_AS(MipsCp0ModelR4000, MipsCp0Model);

}
