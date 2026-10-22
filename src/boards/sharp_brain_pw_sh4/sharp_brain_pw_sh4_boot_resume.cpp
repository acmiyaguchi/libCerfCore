#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include "../../cpu/arm/arm_cp15_registers.h"
#include "../../execution/engine_bridge.h"
#include "../../socs/imx28/imx28_duart.h"
#include "../../socs/imx28/imx28_pinctrl.h"
#include "../../socs/imx28/imx28_power.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <atomic>
#include <cstdint>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 boot_dec.bin 0x1530c (block PA), 0x15310 (block VA); nk.exe 8020edd0. */
constexpr uint32_t kBlockPa = 0x400E8400u;
constexpr uint32_t kBlockVa = 0xA00E8400u;

/* sharp_brain_pw_sh4 boot_dec.bin 0x108b8-0x109c0, 0x10f74. */
constexpr uint32_t kBlockSize    = 0x180u;
constexpr uint32_t kOffImageSum  = 0x170u;
constexpr uint32_t kOffBlockSum  = 0x178u;
constexpr uint32_t kOffResumed   = 0x180u;
constexpr uint32_t kImagePa      = 0x40200000u;
constexpr uint32_t kImageSize    = 0x00A00000u;

/* sharp_brain_pw_sh4 boot_dec.bin 0x1518c-0x151ec. */
constexpr uint32_t kOffControl = 0x48u;
constexpr uint32_t kOffTtbr0   = 0x4Cu;
constexpr uint32_t kOffDacr    = 0x50u;
constexpr uint32_t kOffDfsr    = 0x54u;
constexpr uint32_t kOffIfsr    = 0x58u;
constexpr uint32_t kOffFar     = 0x5Cu;
constexpr uint32_t kOffDtcm    = 0x60u;
constexpr uint32_t kOffItcm    = 0x64u;
constexpr uint32_t kOffTlbLock = 0x68u;
constexpr uint32_t kOffDbgAddr = 0x6Cu;
constexpr uint32_t kOffFcse    = 0x70u;
constexpr uint32_t kOffCid     = 0x74u;

/* sharp_brain_pw_sh4 boot_dec.bin 0x12c-0x148: HW_PINCTRL_CTRL_CLR SFTRST and CLKGATE. */
constexpr uint32_t kPinctrlCtrlClrPa = 0x80018008u;
constexpr uint32_t kSftrstClkgate    = 0xC0000000u;

/* sharp_brain_pw_sh4 boot_dec.bin 0x6b4-0x7b8: PINCTRL MUXSEL6/7, DUART (PL011) IMSC, IBRD, FBRD,
   LCR_H, FR/DR drain, ECR, IFLS, CR. */
constexpr uint32_t kPinctrlMuxsel6SetPa = 0x80018164u;
constexpr uint32_t kPinctrlMuxsel6ClrPa = 0x80018168u;
constexpr uint32_t kPinctrlMuxsel7SetPa = 0x80018174u;
constexpr uint32_t kDuartPa   = 0x80074000u;
constexpr uint32_t kUartDr    = 0x00u;
constexpr uint32_t kUartEcr   = 0x04u;
constexpr uint32_t kUartFr    = 0x18u;
constexpr uint32_t kUartIbrd  = 0x24u;
constexpr uint32_t kUartFbrd  = 0x28u;
constexpr uint32_t kUartLcrH  = 0x2Cu;
constexpr uint32_t kUartCr    = 0x30u;
constexpr uint32_t kUartIfls  = 0x34u;
constexpr uint32_t kUartImsc  = 0x38u;
constexpr uint32_t kFrRxfe    = 1u << 4;

struct BankedRestore {
    uint32_t mode;
    uint32_t spsr_off;
    uint32_t first_reg;
    uint32_t first_off;
};

/* sharp_brain_pw_sh4 boot_dec.bin 0x151f0-0x1529c: FIQ, IRQ, ABT, SYS, SVC. */
constexpr BankedRestore kBanked[] = {
    {0xD1u, 0x78u, 8u, 0x7Cu}, {0xD2u, 0x98u, 8u, 0x9Cu}, {0xD7u, 0xB8u, 13u, 0xBCu},
    {0xDFu, 0u, 13u, 0xC4u},   {0xD3u, 0xCCu, 13u, 0xD0u},
};

/* sharp_brain_pw_sh4 boot_dec.bin 0x152bc-0x15308; ARM DDI 0100I A4.1.39 (MSR fields c and f). */
constexpr uint32_t kOffCpsr  = 0x40u;
constexpr uint32_t kOffSpsr  = 0x44u;
constexpr uint32_t kOffR1    = 0x04u;
constexpr uint32_t kMsrFc    = 0xFF0000FFu;
constexpr uint32_t kModeMask = 0x1Fu;
constexpr uint32_t kModeUsr  = 0x10u;
constexpr uint32_t kModeSys  = 0x1Fu;

class SharpBrainPwSh4BootResume : public Service, public DeepSleepWaker {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        emu_.Get<GuestDeepSleep>().RegisterWaker(this);
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            if (resume_.exchange(false)) Resume();
        });
    }

    void LatchSleepWakeCause() override { resume_.store(true); }
    void ClearSleepWakeCause() override { resume_.store(false); }

    /* MCIMX28RM Rev. 2 section 11.12.16 PWD; sharp_brain_pw_sh4 boot_dec.bin 0x108fc-0x109b8. */
    bool WakeResumes() override {
        return emu_.Get<Imx28Power>().SleptByPwd() && BlockValid();
    }

private:
    uint32_t Block(uint32_t off) { return emu_.Get<EmulatedMemory>().ReadWord(kBlockPa + off); }

    uint32_t ByteSum(uint32_t pa, uint32_t size) {
        const uint8_t* p = emu_.Get<EmulatedMemory>().TryTranslateRange(pa, size);
        if (p == nullptr) emu_.Get<Fatal>().Die("PW-SH4 resume: no RAM at %08X size %08X", pa, size);
        uint32_t sum = 0u;
        for (uint32_t i = 0; i < size; ++i) sum += p[i];
        return sum;
    }

    void InitDuart() {
        auto& u = emu_.Get<Imx28Duart>();
        const auto rd = [&](uint32_t off) { return u.ReadWord(kDuartPa + off); };
        const auto wr = [&](uint32_t off, uint32_t v) { u.WriteWord(kDuartPa + off, v); };
        wr(kUartImsc, 0u);
        auto& pin = emu_.Get<Imx28Pinctrl>();
        pin.WriteWord(kPinctrlMuxsel7SetPa, 0x000F000Fu);
        pin.WriteWord(kPinctrlMuxsel6SetPa, 0xC0u);
        pin.WriteWord(kPinctrlMuxsel6ClrPa, 0x40u);
        wr(kUartIbrd, (rd(kUartIbrd) & 0xFFFF0000u) | 0x0Du);
        wr(kUartFbrd, (rd(kUartFbrd) & ~0x3FFu) | 0x341u);
        wr(kUartLcrH, 0x70u);
        while ((rd(kUartFr) & kFrRxfe) == 0u) rd(kUartDr);
        wr(kUartEcr, rd(kUartEcr) | 0xF0u);
        wr(kUartIfls, 0x09u);
        wr(kUartCr, 0x301u);
    }

    /* sharp_brain_pw_sh4 boot_dec.bin 0x108fc-0x109b8. */
    bool BlockValid() {
        const uint32_t size  = Block(0u);
        const uint32_t bsum  = ByteSum(kBlockPa, kOffBlockSum);
        const uint32_t isum  = ByteSum(kImagePa, kImageSize);
        const bool     valid = size == kBlockSize && Block(kOffBlockSum) == bsum &&
                               Block(kOffBlockSum + 4u) == ~bsum &&
                               Block(kOffImageSum) == isum && Block(kOffImageSum + 4u) == ~isum;
        if (!valid) {
            LOG(SocReset, "[DEEPSLEEP] PW-SH4 resume: save block rejected (size %08X, block sum "
                          "%08X %08X computed %08X, image sum %08X %08X computed %08X)\n",
                size, Block(kOffBlockSum), Block(kOffBlockSum + 4u), bsum, Block(kOffImageSum),
                Block(kOffImageSum + 4u), isum);
        }
        return valid;
    }

    void Resume() {
        emu_.Get<Imx28Pinctrl>().WriteWord(kPinctrlCtrlClrPa, kSftrstClkgate);
        InitDuart();
        emu_.Get<EmulatedMemory>().WriteWord(kBlockPa + kOffResumed, 1u);
        if (Block(kOffFcse) != 0u || (Block(kOffDtcm) & 1u) != 0u || (Block(kOffItcm) & 1u) != 0u ||
            (Block(kOffTlbLock) & 1u) != 0u) {
            emu_.Get<Fatal>().Die("PW-SH4 resume: FCSE PID %08X, TCM regions %08X %08X, TLB lockdown "
                                  "%08X", Block(kOffFcse), Block(kOffDtcm), Block(kOffItcm),
                                  Block(kOffTlbLock));
        }
        auto& cp15 = emu_.Get<ArmCp15Registers>();
        cp15.File().dfsr       = Block(kOffDfsr);
        cp15.File().ifsr       = Block(kOffIfsr);
        cp15.File().dfar       = Block(kOffFar);
        cp15.File().context_id = Block(kOffCid);
        cp15.File().tcm_region[0]      = Block(kOffDtcm);
        cp15.File().tcm_region[1]      = Block(kOffItcm);
        cp15.File().tlb_lockdown       = Block(kOffTlbLock);
        cp15.File().debug_test_address = Block(kOffDbgAddr);
        cp15.SetPendingResumeMmu(Block(kOffControl), Block(kOffTtbr0), Block(kOffDacr));

        auto& engine = emu_.Get<EngineBridge>().Engine();
        auto& cpu    = engine.Get<cerfengine::ArmCpu>();
        auto* st     = cpu.State();
        for (const auto& b : kBanked) {
            cerfengine::ArmPsrFull psr;
            psr.word = b.mode;
            cpu.UpdateCpsrWithFlags(psr);
            if (b.spsr_off != 0u) cpu.BankedSpsr(b.mode & kModeMask)->word = Block(b.spsr_off);
            for (uint32_t r = b.first_reg; r <= 14u; ++r) {
                st->gprs[r] = Block(b.first_off + 4u * (r - b.first_reg));
            }
        }
        cerfengine::ArmPsrFull cpsr;
        cpsr.word = (cerfengine::ArmPackCpsr(*st) & ~kMsrFc) | (Block(kOffCpsr) & kMsrFc);
        const uint32_t mode = cpsr.bits.mode;
        if (mode == kModeUsr || mode == kModeSys) {
            emu_.Get<Fatal>().Die("PW-SH4 resume: SPSR write in CPSR mode %02X (CPSR %08X)", mode,
                                  cpsr.word);
        }
        cpu.UpdateCpsrWithFlags(cpsr);
        cpu.BankedSpsr(mode)->word = Block(kOffSpsr);
        st->gprs[0] = kBlockVa;
        for (uint32_t r = 1u; r <= 14u; ++r) st->gprs[r] = Block(kOffR1 + 4u * (r - 1u));
        engine.Get<cerfengine::ArmMmu>().InvalidateAllTlbs();
        cpu.SetPendingResumeVector(st->gprs[14]);
        cpu.SetPendingResumeCpsr(cpsr.word);
        LOG(SocReset, "[DEEPSLEEP] PW-SH4 resume: pc %08X cpsr %08X control %08X ttbr0 %08X\n",
            st->gprs[14], cpsr.word, Block(kOffControl), Block(kOffTtbr0));
    }

    std::atomic<bool> resume_{false};
};

}  /* namespace */

REGISTER_SERVICE(SharpBrainPwSh4BootResume);

}
