#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <mutex>

namespace cerfengine { class ArmJit; }

class Sa11xxIntc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x90050000u; }
    uint32_t MmioSize() const override { return 0x00010000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* §9.2.1.1 source bit assignments - used by SoC peripheral
       impls to wire their interrupt source into the controller. */
    void AssertSource(uint32_t bit_index);
    void DeassertSource(uint32_t bit_index);

    /* Level source: set ICPR bits within `mask` to `level` in one atomic update
       (used by the OS timer, whose §9.4 interrupt follows OSSR&OIER as a level). */
    void SetSourceLevel(uint32_t mask, uint32_t level);

    /* Diagnostic snapshot - locks state_mtx_ so all four are
       consistent for a trace handler. */
    uint32_t GetIcpr() const;
    uint32_t GetIcmr() const;
    uint32_t GetIclr() const;
    uint32_t GetIcIp() const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    /* Drop and host-thread AssertSource racing JIT-thread WriteReg
       commits a stale jit.SetInterruptPending after the mask write
       already cleared the line - guest spins in OEMIH returning
       SYSINTR_NOP forever. */
    mutable std::mutex state_mtx_;

    cerfengine::ArmJit*  jit_  = nullptr;
    uint32_t icpr_ = 0;
    uint32_t icmr_ = 0;
    uint32_t iclr_ = 0;
    uint32_t iccr_ = 0;

    uint32_t IcIpLocked() const { return icpr_ & icmr_ & ~iclr_; }
    uint32_t IcFpLocked() const { return icpr_ & icmr_ & iclr_; }
    /* SA-1110 Dev Man §9.2.1.5 ICCR.DIM: 0 - all enabled interrupts bring the
       SA-1110 out of idle mode; 1 - only enabled and unmasked (per the ICMR). */
    bool IdleWakeLocked() const {
        return (iccr_ & 0x1u) != 0u ? IcIpLocked() != 0u : icpr_ != 0u;
    }

    void NotifyLocked();

    uint32_t ReadRegLocked(uint32_t off) const;
    void     WriteRegLocked(uint32_t off, uint32_t value);

    static bool IsKnown(uint32_t off) {
        return off == 0x00 || off == 0x04 || off == 0x08 ||
               off == 0x0C || off == 0x10 || off == 0x20;
    }
};
