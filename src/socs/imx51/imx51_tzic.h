#pragma once

#include "../irq_controller.h"

#include <cstdint>
#include <mutex>

namespace cerfengine { class ArmJit; }
class StateWriter;
class StateReader;

/* i.MX51 TZIC (TrustZone Interrupt Controller), MCIMX51RM Ch.57: 128 sources in
   4 banks of 32. Stateful logic; the sibling Imx51TzicMmio Peripheral delegates
   MMIO + hibernation here (same split as Omap3530Intc). */
class Imx51Tzic : public IrqController {
public:
    using IrqController::IrqController;

    bool ShouldRegister() override;
    void OnReady() override;

    void AssertIrq   (int source_bit)            override;
    void AssertSubIrq(int main_bit, int sub_bit) override;
    void DeAssertIrq (int source_bit)            override;

    uint32_t ReadReg (uint32_t off);
    void     WriteReg(uint32_t off, uint32_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    /* Highest-priority deliverable source, or -1 (caller holds state_mutex_).
       Deliverable = pending (raw|swforce) & enabled, controller-enabled for its
       domain (EN/NSEN), priority < PRIOMASK (RM §57.3.3.4). Priority 0 is the
       highest (RM p57-22), so the lowest priority value wins. */
    int  ComputeActiveSource() const;
    bool HasPendingUnmasked() const;
    bool SourceDeliverable(uint32_t src) const;

    /* Bank index for an offset in a 4-register set-region [base, base+0x10),
       or -1 if outside it. */
    static int BankInSet(uint32_t off, uint32_t base);

    static constexpr uint32_t kSourceCount = 128;
    static constexpr uint32_t kBankCount   = 4;
    static constexpr uint32_t kBitsPerBank = 32;

    static constexpr uint32_t kOffIntctrl   = 0x000;  /* INTCTRL  R/W */
    static constexpr uint32_t kOffInttype   = 0x004;  /* INTTYPE  RO  */
    /* PRIOMASK/SYNCCTRL/DSMINT at 0x00C/0x010/0x014 - RM Table 57-1; 0x008 is a
       reserved gap between INTTYPE (0x004) and PRIOMASK (0x00C). */
    static constexpr uint32_t kOffPriomask  = 0x00C;  /* PRIOMASK R/W */
    static constexpr uint32_t kOffSyncctrl  = 0x010;  /* SYNCCTRL R/W */
    static constexpr uint32_t kOffDsmint    = 0x014;  /* DSMINT   R/W */
    static constexpr uint32_t kOffIntsec0   = 0x080;  /* INTSEC0-3   */
    static constexpr uint32_t kOffEnset0    = 0x100;  /* ENSET0-3    */
    static constexpr uint32_t kOffEnclear0  = 0x180;  /* ENCLEAR0-3  */
    static constexpr uint32_t kOffSrcset0   = 0x200;  /* SRCSET0-3   */
    static constexpr uint32_t kOffSrcclear0 = 0x280;  /* SRCCLEAR0-3 */
    static constexpr uint32_t kOffPriority0 = 0x300;  /* PRIORITY0-31 (4 src/reg) */
    static constexpr uint32_t kOffPnd0      = 0xD00;  /* PND0-3   RO  */
    static constexpr uint32_t kOffHipnd0    = 0xD80;  /* HIPND0-3 RO  */
    static constexpr uint32_t kOffWakeup0   = 0xE00;  /* WAKEUP0-3   */

    static constexpr uint32_t kInttype      = 0x00000403u;  /* RO, Table 57-1 reset */
    static constexpr uint32_t kIntctrlEn    = 1u << 0;      /* secure enable */
    static constexpr uint32_t kIntctrlNsen  = 1u << 16;     /* non-secure enable */
    static constexpr uint32_t kIntctrlNsenMask = 1u << 31;  /* NSEN write-enable */

    mutable std::mutex state_mutex_;
    cerfengine::ArmJit*  jit_ = nullptr;
    uint32_t raw_[kBankCount]{};       /* raw input level (AssertIrq) */
    uint32_t enable_[kBankCount]{};    /* ENSET/ENCLEAR; 1 = enabled */
    uint32_t secure_[kBankCount]{};    /* INTSEC; 1 = secure */
    uint32_t swforce_[kBankCount]{};   /* SRCSET/SRCCLEAR */
    /* WAKEUP0-3 (RM Table 57-1): which sources may wake from low-power. Stored
       only - CERF has no real suspend (it wakes instantly), so the config never
       gates interrupt delivery. */
    uint32_t wakeup_[kBankCount]{};
    uint32_t priority_[32]{};          /* PRIORITY0-31 */
    uint32_t intctrl_  = 0;
    uint32_t priomask_ = 0;
    uint32_t syncctrl_ = 0;
    uint32_t dsmint_   = 0;
};
