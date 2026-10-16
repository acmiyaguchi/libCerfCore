#include "mips_engine_bridge.h"

#include <utility>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../cpu/mips/mips_cp0_model.h"
#include "../cpu/mips/mips_cp0_ops.h"
#include "../cpu/mips/mips_exception_model.h"
#include "../cpu/mips/mips_tlb_model.h"
#include "../cpu/mips_processor_config.h"
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_jit.h>
#include <cerfcore/state/state_stream.h>
#include "../tracing/trace_manager.h"
#include "guest_cycle_clock.h"

REGISTER_SERVICE_AS(MipsEngineBridge, EngineBridge);

namespace {

template <typename F>
constexpr void VisitCpuSection(cerfengine::MipsCpuState& s, F& field) {
    field("gpr", s.gpr);
    field("hi", s.hi);
    field("lo", s.lo);
    field("pc", s.pc);
    field("branch_state", s.branch_state);
    field("btarget", s.btarget);
    field("bcond", s.bcond);
    field("cp0_index", s.cp0_index);
    field("cp0_random", s.cp0_random);
    field("cp0_entrylo0", s.cp0_entrylo0);
    field("cp0_entrylo1", s.cp0_entrylo1);
    field("cp0_context", s.cp0_context);
    field("cp0_pagemask", s.cp0_pagemask);
    field("cp0_wired", s.cp0_wired);
    field("cp0_badvaddr", s.cp0_badvaddr);
    field("count", s.count_save);
    field("cp0_entryhi", s.cp0_entryhi);
    field("cp0_compare", s.cp0_compare);
    field("cp0_status", s.cp0_status);
    field("cp0_cause", s.cp0_cause);
    field("cp0_epc", s.cp0_epc);
    field("cp0_prid", s.cp0_prid);
    field("cp0_config", s.cp0_config);
    field("cp0_watchlo", s.cp0_watchlo);
    field("cp0_watchhi", s.cp0_watchhi);
    field("cp0_taglo", s.cp0_taglo);
    field("cp0_taghi", s.cp0_taghi);
    field("cp0_errorepc", s.cp0_errorepc);
    field("cp0_lladdr", s.cp0_lladdr);
    field("cp0_xcontext", s.cp0_xcontext);
    field("cp0_ecc", s.cp0_ecc);
    for (cerfengine::MipsTlbEntry& e : s.tlb) {
        field("tlb_vpn", e.vpn);
        field("tlb_page_mask", e.page_mask);
        field("tlb_asid", e.asid);
        field("tlb_g", e.g);
        field("tlb_v0", e.v0);
        field("tlb_d0", e.d0);
        field("tlb_c0", e.c0);
        field("tlb_v1", e.v1);
        field("tlb_d1", e.d1);
        field("tlb_c1", e.c1);
        field.Skip(e.pad);
        field("tlb_pfn", e.pfn);
    }
    field("nb_tlb", s.nb_tlb);
    field("tlb_in_use", s.tlb_in_use);
    field("llbit", s.llbit);
    field("ll_addr", s.ll_addr);
    field("reset_pending", s.reset_pending);
    field("deep_sleep", s.deep_sleep);
    field("guest_cycle_counter", s.guest_cycle_counter);
    field.Skip(s.guest_cycle_deadline);
    field("count_phase", s.count_save_phase);
    field("guest_cycle_hi", s.guest_cycle_hi);
    field("guest_cycle_folded", s.guest_cycle_folded);
    field("count_phase_den", s.count_save_phase_den);
    field("min_page_shift", s.min_page_shift);
    field("phys_addr_mask", s.phys_addr_mask);
    field("isa_mode", s.isa_mode);
    field("btarget_isa", s.btarget_isa);
    field("branch_len", s.branch_len);
    field("idle_kind", s.idle_wait);
    field.Skip(s.idle_pad);
}

}

bool MipsEngineBridge::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kMips;
}

void MipsEngineBridge::FillSetup(cerfengine::Setup& setup) {
    const MipsProcessorConfig& cfg = emu_.Get<MipsProcessorConfig>();
    setup.isa                     = cerfengine::Isa::kMips;
    setup.mips.isa_level          = cfg.IsaLevel();
    setup.mips.fpu                = cfg.HasFpu();
    setup.mips.llsc               = cfg.HasLlsc();
    setup.mips.mips16             = cfg.HasMips16();
    setup.mips.tx39_multiply_add  = cfg.HasTx39MultiplyAdd();
    setup.mips.vr41xx_power_modes = cfg.HasVr41xxPowerModes();
    setup.mips.device_ip_mask     = cfg.DeviceIpMask();
    setup.mips.phys_addr_mask     = cfg.PhysAddrMask();
    setup.mips_tlb                = emu_.Get<MipsTlbModel>().Hooks();
    setup.mips_exception          = emu_.Get<MipsExceptionModel>().Hooks();
    setup.mips_system             = this;
}

void MipsEngineBridge::ColdBoot() {
    cerfengine::MipsCpu& cpu = Engine().Get<cerfengine::MipsCpu>();
    cpu.ResetState();
    LOG(Jit, "MipsEngineBridge::ColdBoot: entry VA=0x%08X\n", cpu.State()->pc);
}

void MipsEngineBridge::MipsReset(cerfengine::MipsCpuState* st) {
    const MipsProcessorConfig& cfg = emu_.Get<MipsProcessorConfig>();
    st->cp0_prid       = cfg.Prid();
    st->nb_tlb         = cfg.TlbSize();
    st->tlb_in_use     = st->nb_tlb;
    st->min_page_shift = cfg.MinPageShift();
    st->pc             = emu_.Get<RomParserService>().EntryVa();
    for (auto& fn : reset_listeners_) fn();
    for (auto& fn : post_reset_listeners_) fn();
}

cerfengine::MipsCp0Decision MipsEngineBridge::MipsCp0(uint32_t rd) {
    return emu_.Get<MipsCp0Model>().Decide(rd);
}

void MipsEngineBridge::RegisterResetListener(std::function<void()> fn) {
    reset_listeners_.push_back(std::move(fn));
}

void MipsEngineBridge::RegisterPostResetListener(std::function<void()> fn) {
    post_reset_listeners_.push_back(std::move(fn));
}

void MipsEngineBridge::RegisterRestoreListener(std::function<void()> fn) {
    restore_listeners_.push_back(std::move(fn));
}

void MipsEngineBridge::SaveCpuState(StateWriter& w) {
    static_assert(StateVisitCoversAllBytes<cerfengine::MipsCpuState>(
                      [](cerfengine::MipsCpuState& s, StateFieldBytes& f) { VisitCpuSection(s, f); }),
                  "the MIPS CPU visitor must name or skip every field of MipsCpuState");
    cerfengine::MipsJit& jit = Engine().Get<cerfengine::MipsJit>();
    jit.PrepareStateSave();
    emu_.Get<MipsCp0Ops>().SaveCount();
    StateWriteField field(w);
    VisitCpuSection(*jit.CpuState(), field);
}

void MipsEngineBridge::RestoreCpuState(StateReader& r) {
    cerfengine::MipsJit& jit = Engine().Get<cerfengine::MipsJit>();
    StateReadField field(r);
    VisitCpuSection(*jit.CpuState(), field);
    emu_.Get<GuestCycleClock>().OnCyclesRestored();
    jit.OnStateRestored();
    emu_.Get<MipsCp0Ops>().OnCpuStateRestored();
    for (auto& fn : restore_listeners_) fn();
}

void MipsEngineBridge::SaveMmuState(StateWriter& w) {
    emu_.Get<MipsTlbModel>().SaveState(w);
}

void MipsEngineBridge::RestoreMmuState(StateReader& r) {
    emu_.Get<MipsTlbModel>().RestoreState(r);
}

void MipsEngineBridge::MapInjectionBand(uint32_t va, uint32_t pa, uint32_t size) {
    Engine().Get<cerfengine::MipsJit>().SetFixedWindow(va, pa, size);
}

void MipsEngineBridge::DispatchTraceIter() {
#if CERF_DEV_MODE
    Trace().DispatchRunLoopIterMips(Engine().Get<cerfengine::MipsJit>().CpuState());
#endif
}
