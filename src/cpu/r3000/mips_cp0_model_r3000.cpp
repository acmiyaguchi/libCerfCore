#include "../mips/mips_cp0_model.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <cerfcore/boards/board_context.h>
#include "../../socs/pr31x00/pr31500_id.h"
#include "../../socs/pr31x00/pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips/mips_cp0_ops.h"
#include "../mips_processor_config.h"
#include "tx39_config_register.h"

namespace cerfcore {

namespace {

/* TX39 CP0 register numbers (TMPR39xx-um Table 2-1). */
namespace Tx39Cp0 {
    constexpr uint32_t kIndex    = 0;
    constexpr uint32_t kRandom   = 1;
    constexpr uint32_t kEntryLo  = 2;
    constexpr uint32_t kConfig   = 3;
    constexpr uint32_t kContext  = 4;
    constexpr uint32_t kBadVAddr = 8;
    constexpr uint32_t kEntryHi  = 10;
    constexpr uint32_t kStatus   = 12;
    constexpr uint32_t kCause    = 13;
    constexpr uint32_t kEPC      = 14;
    constexpr uint32_t kPRId     = 15;
}

int32_t RegOffset(uint32_t rd) {
    switch (rd) {
        case Tx39Cp0::kIndex:    return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_index));
        case Tx39Cp0::kRandom:   return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_random));
        case Tx39Cp0::kEntryLo:  return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_entrylo0));
        case Tx39Cp0::kConfig:   return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_config));
        case Tx39Cp0::kContext:  return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_context));
        case Tx39Cp0::kBadVAddr: return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_badvaddr));
        case Tx39Cp0::kEntryHi:  return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_entryhi));
        case Tx39Cp0::kStatus:   return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_status));
        case Tx39Cp0::kCause:    return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_cause));
        case Tx39Cp0::kEPC:      return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_epc));
        case Tx39Cp0::kPRId:     return static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_prid));
        default:                 return -1;
    }
}

class MipsCp0ModelR3000 : public MipsCp0Model {
public:
    using MipsCp0Model::MipsCp0Model;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }

    void OnReady() override {
        config_     = &emu_.Get<MipsProcessorConfig>();
        ops_        = &emu_.Get<MipsCp0Ops>();
        config_reg_ = &emu_.Get<Tx39ConfigRegister>();
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
        if (rd == Tx39Cp0::kRandom) {
            d.read         = &MipsCp0Ops::ReadRandom;
            d.read_context = ops_;
        }
        if (rd == Tx39Cp0::kEntryHi) {
            d.write         = &MipsCp0Ops::WriteEntryHiR3000;
            d.write_context = ops_;
        }
        if (rd == Tx39Cp0::kConfig) {
            d.write         = &Tx39ConfigRegister::WriteConfig;
            d.write_context = config_reg_;
        }
        return d;
    }

private:
    MipsProcessorConfig* config_     = nullptr;
    MipsCp0Ops*          ops_        = nullptr;
    Tx39ConfigRegister*  config_reg_ = nullptr;
};

}

REGISTER_SERVICE_AS(MipsCp0ModelR3000, MipsCp0Model);

}
