#include "engine_bridge.h"

#include <cerfcore/boards/board_context.h>
#include "../boot/boot_mode.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../cpu/arm/arm_coproc_provider.h"
#include "../cpu/arm/arm_cp15_registers.h"
#include "../cpu/arm/arm_injection_band.h"
#include "../cpu/arm/arm_instruction_timing_model.h"
#include "../cpu/arm/arm_page_walker.h"
#include "../cpu/arm_processor_config.h"
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_system_host.h>
#include <cerfcore/state/state_stream.h>
#include "../tracing/trace_manager.h"
#include "guest_cycle_clock.h"

namespace {

template <typename F>
constexpr void VisitCpuHead(cerfengine::ArmCpuState& s, F& field) {
    field("gprs", s.gprs);
    field("cpsr", s.cpsr.word);
    field("nf", s.nf);
    field("zf", s.zf);
    field("cf", s.cf);
    field("vf", s.vf);
    field("sp_bank", s.sp_bank);
    field("lr_bank", s.lr_bank);
    for (cerfengine::ArmPsrFull& spsr : s.spsr_bank) field("spsr_bank", spsr.word);
    field("r8_r12_fiq", s.r8_r12_fiq);
    field("vfp_d", s.vfp_d);
    field("fpscr", s.fpscr);
    field("fpexc", s.fpexc);
}

template <typename F>
constexpr void VisitCpuTail(cerfengine::ArmCpuState& s, F& field) {
    field("ldrex_monitor_addr", s.ldrex_monitor_addr);
    field("ldrex_monitor_armed", s.ldrex_monitor_armed);
    field("guest_cycle_counter", s.guest_cycle_counter);
    field.Skip(s.guest_cycle_deadline);
    field("guest_cycle_hi", s.guest_cycle_hi);
    field("guest_cycle_folded", s.guest_cycle_folded);
    field("irq_interrupt_pending", s.irq_interrupt_pending);
    field("reset_pending", s.reset_pending);
    field("deep_sleep", s.deep_sleep);
    field("idle_wait", s.idle_wait);
    field.Skip(s.chain_exit_request);
    field.Skip(s.pad);
}

template <typename F>
constexpr void VisitMmuControl(cerfengine::ArmMmuState& s, F& field) {
    field("control_register", s.control_register.word);
    field("effective_control_register", s.effective_control_register.word);
}

template <typename F>
constexpr void VisitMmuProcess(cerfengine::ArmMmuState& s, F& field) {
    field("process_id", s.process_id);
    field.Skip(s.fcse_fold_id);
    field("coprocessor_access", s.coprocessor_access);
}

template <typename F>
constexpr void VisitMmuContext(cerfengine::ArmMmuState& s, F& field) {
    field("contextidr", s.contextidr);
    field.Skip(s.data_tlb);
    field.Skip(s.instruction_tlb);
    field.Skip(s.code_word_base);
    field.Skip(s.code_word_top);
    field.Skip(s.code_word_bitmap_bytes);
    field.Skip(s.code_page_dirty_bytes);
    field.Skip(s.code_xlat_bitmap);
    field.Skip(s.code_page_dirty);
}

template <typename F>
void VisitCpuSection(cerfengine::ArmCpuState& s, uint64_t& acc0, F& field) {
    VisitCpuHead(s, field);
    field("acc0", acc0);
    VisitCpuTail(s, field);
}

template <typename F>
void VisitMmuSection(cerfengine::ArmMmuState& s, ArmCp15File& cp15, F& field) {
    VisitMmuControl(s, field);
    ArmCp15Registers::VisitFaultRegisters(cp15, field);
    VisitMmuProcess(s, field);
    ArmCp15Registers::VisitTableRegisters(cp15, field);
    VisitMmuContext(s, field);
    ArmCp15Registers::VisitThreadRegisters(cp15, field);
}

class ArmEngineBridge : public EngineBridge, public cerfengine::ArmSystemHost {
public:
    using EngineBridge::EngineBridge;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm;
    }

    void SaveCpuState(StateWriter& w) override {
        static_assert(StateVisitCoversAllBytes<cerfengine::ArmCpuState>(
                          [](cerfengine::ArmCpuState& s, StateFieldBytes& f) {
                              VisitCpuHead(s, f);
                              VisitCpuTail(s, f);
                          }),
                      "the ARM CPU visitors must name or skip every field of ArmCpuState");
        StateWriteField field(w);
        VisitCpuSection(*Engine().Get<cerfengine::ArmCpu>().State(),
                        emu_.Get<ArmCoprocProvider>().Accumulator(), field);
    }

    void RestoreCpuState(StateReader& r) override {
        cerfengine::ArmCpu& cpu = Engine().Get<cerfengine::ArmCpu>();
        StateReadField field(r);
        VisitCpuSection(*cpu.State(), emu_.Get<ArmCoprocProvider>().Accumulator(), field);
        cpu.OnStateRestored();
        emu_.Get<GuestCycleClock>().OnCyclesRestored();
    }

    void SaveMmuState(StateWriter& w) override {
        static_assert(StateVisitCoversAllBytes<cerfengine::ArmMmuState>(
                          [](cerfengine::ArmMmuState& s, StateFieldBytes& f) {
                              VisitMmuControl(s, f);
                              VisitMmuProcess(s, f);
                              VisitMmuContext(s, f);
                          }),
                      "the ARM MMU visitors must name or skip every field of ArmMmuState");
        static_assert(StateVisitCoversAllBytes<ArmCp15File>(
                          [](ArmCp15File& s, StateFieldBytes& f) {
                              ArmCp15Registers::VisitFaultRegisters(s, f);
                              ArmCp15Registers::VisitTableRegisters(s, f);
                              ArmCp15Registers::VisitThreadRegisters(s, f);
                          }),
                      "the cp15 visitors must name every field of ArmCp15File");
        StateWriteField field(w);
        VisitMmuSection(*Engine().Get<cerfengine::ArmMmu>().State(), cp15_->File(), field);
    }

    void RestoreMmuState(StateReader& r) override {
        cerfengine::ArmMmu& mmu = Engine().Get<cerfengine::ArmMmu>();
        StateReadField field(r);
        VisitMmuSection(*mmu.State(), cp15_->File(), field);
        mmu.RefreshFcseFold();
        mmu.InvalidateAllTlbs();
    }

    void DispatchTraceIter() override {
#if CERF_DEV_MODE
        const cerfengine::ArmCpuState* st = Engine().Get<cerfengine::ArmCpu>().State();
        Trace().DispatchRunLoopIter(st->gprs, cerfengine::ArmPackCpsr(*st));
#endif
    }

    void MapInjectionBand(uint32_t va, uint32_t pa, uint32_t size) override {
        emu_.Get<ArmInjectionBand>().Set(va, pa, size);
    }

    cerfengine::ArmCoprocDecision ArmCoprocessor(const cerfengine::ArmCoprocAccess& a) override {
        return emu_.Get<ArmCoprocProvider>().Decide(a);
    }

    cerfengine::ArmResetResult ArmReset() override { return cp15_->Reset(); }

protected:
    void FillSetup(cerfengine::Setup& setup) override {
        const ArmProcessorConfig& cfg = emu_.Get<ArmProcessorConfig>();
        cp15_ = &emu_.Get<ArmCp15Registers>();

        cerfengine::ArmFeatures& f      = setup.arm;
        f.arch_v6                       = cfg.HasCp15V6();
        f.arch_v7                       = cfg.HasCp15V7();
        f.thumb                         = cfg.HasThumb();
        f.thumb2                        = cfg.HasThumb2();
        f.blx_reg                       = cfg.HasBlxReg();
        f.dsp                           = cfg.HasDsp();
        f.neon                          = cfg.HasNeon();
        f.vfp                           = cfg.HasVfp();
        f.integer_divide                = cfg.HasIntegerDivide();
        f.bit_field                     = cfg.HasBitField();
        f.rev                           = cfg.HasRev();
        f.extend_rotate                 = cfg.HasExtendRotate();
        f.movw_movt                     = cfg.HasMovwMovt();
        f.clz                           = cfg.HasClz();
        f.security_extensions           = cfg.HasSecurityExtensions();
        f.ldrex_strex                   = cfg.HasLdrexStrex();
        f.ldrex_strex_v6k               = cfg.HasLdrexStrexV6k();
        f.armv5_unconditional_space     = cfg.HasArmv5UnconditionalSpace();
        f.mls                           = cfg.HasMls();
        f.barrier_insn                  = cfg.HasBarrierInsn();
        f.preload                       = cfg.HasPreload();
        f.load_store_double             = cfg.HasLoadStoreDouble();
        f.load_to_pc_interworking       = cfg.HasLoadToPcInterworking();
        f.data_proc_to_pc_interworking  = cfg.HasDataProcToPcInterworking();
        f.accesses_spsr_in_system_mode  = cfg.AccessesSpsrInSystemMode();
        f.base_restored_abort_model     = cfg.BaseRestoredAbortModel();
        f.pc_store_offset               = cfg.PcStoreOffset();
        f.fpsid                         = cfg.Fpsid();
        f.mvfr0                         = cfg.Mvfr0();
        f.mvfr1                         = cfg.Mvfr1();

        setup.isa                 = cerfengine::Isa::kArm;
        setup.arm_translation     = &emu_.Get<ArmPageWalker>();
        setup.arm_alignment_fault = cp15_;
        setup.arm_system          = this;
        setup.arm_timing          = emu_.TryGet<ArmInstructionTimingModel>();
    }

    void ColdBoot() override {
        BootMode&      boot       = emu_.Get<BootMode>();
        const uint32_t cold_entry = boot.ColdEntryPa();
        const uint32_t cold_stack = boot.ColdStackPa();
        cerfengine::ArmCpu&        cpu        = Engine().Get<cerfengine::ArmCpu>();
        cpu.SetInitialStackPointer(cold_stack);
        cpu.RaiseResetException(cold_entry, boot.ColdEntryThumb());
        LOG(Jit, "ArmEngineBridge::ColdBoot: cold entry PA 0x%08X (%s), cold stack PA 0x%08X\n",
            cold_entry, boot.ColdEntryThumb() ? "Thumb" : "ARM", cold_stack);
    }

private:
    ArmCp15Registers* cp15_ = nullptr;
};

}

REGISTER_SERVICE_AS(ArmEngineBridge, EngineBridge);
