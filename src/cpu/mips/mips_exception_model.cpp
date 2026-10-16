#include "mips_exception_model.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include "../mips_processor_config.h"

namespace cerfcore {

void MipsExceptionModel::OnReady() {
    config_ = &emu_.Get<MipsProcessorConfig>();
}

void CERFENGINE_FASTCALL MipsExceptionModel::EretHook(void* model,
                                                      cerfengine::MipsCpuState* st) {
    cerfengine::MipsCpuState& s = *st;
    /* MIPS64 Vol2 ERET: ERL takes precedence over EXL. */
    if ((s.cp0_status >> cerfengine::MipsStatusBit::kERL) & 1u) {
        s.pc = s.cp0_errorepc;
        s.cp0_status &= ~(1u << cerfengine::MipsStatusBit::kERL);
    } else {
        s.pc = s.cp0_epc;
        s.cp0_status &= ~(1u << cerfengine::MipsStatusBit::kEXL);
    }
    if (static_cast<MipsExceptionModel*>(model)->config_->HasMips16()) {
        s.isa_mode = s.pc & 1u;
        s.pc &= ~1u;
    }
    s.llbit = 0;
}

void CERFENGINE_FASTCALL MipsExceptionModel::RfeHook(void*, cerfengine::MipsCpuState* st) {
    cerfengine::MipsCpuState& s = *st;
    /* IEc<-IEp, KUc<-KUp, IEp<-IEo, KUp<-KUo; IEo and KUo retain their values
       (TMPR39xx-um Fig 6-7). Status bits: IEc<0> KUc<1> IEp<2> KUp<3> IEo<4>
       KUo<5> (TMPR39xx-um §6.2.3). */
    s.cp0_status = (s.cp0_status & ~0xFu) | ((s.cp0_status >> 2) & 0xFu);
}

}
