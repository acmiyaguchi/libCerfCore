#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "omap3530_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../irq_controller.h"
#include "omap3530_gpio_bus.h"
#include "omap3530_reg_reset.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <mutex>
#include <vector>

class Omap3530PrcmStubBlock : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Omap3530;
    }
    void OnReady() override {
        regs_.resize(MmioSize() / 4u, 0u);
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t ReadWord (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mu_);
        SaveRegsLocked(w);
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mu_);
        RestoreRegsLocked(r);
    }

protected:
    virtual const char* Label() const = 0;
    virtual const char* RegisterName(uint32_t off) const = 0;

    /* regs_ is sized in OnReady (MmioSize/4), which runs before restore, so
       the blob length matches on both sides for the same concrete. */
    void SaveRegsLocked(StateWriter& w) const {
        w.WriteBytes("regs", regs_.data(), regs_.size() * sizeof(uint32_t));
    }
    void RestoreRegsLocked(StateReader& r) {
        r.ReadBytes("regs", regs_.data(), regs_.size() * sizeof(uint32_t));
    }

    uint32_t PeekReg(uint32_t off) const {
        std::lock_guard<std::mutex> lk(mu_);
        return regs_[off / 4u];
    }

    static constexpr uint32_t LaneShift(uint32_t off) { return (off & 3u) * 8u; }

    static uint32_t MergeHalf(uint32_t word, uint32_t off, uint16_t value) {
        const uint32_t shift = LaneShift(off);
        const uint32_t mask  = 0xFFFFu << shift;
        return (word & ~mask) | (static_cast<uint32_t>(value) << shift);
    }

    static uint16_t HalfOf(uint32_t word, uint32_t off) { return static_cast<uint16_t>(word >> LaneShift(off)); }

    uint32_t HalfOffset(const char* op, uint32_t addr, uint64_t value) const {
        const uint32_t off = addr - MmioBase();
        if (off & 1u) HaltUnsupportedAccess(op, addr, value);
        return off;
    }

    uint32_t WordWithHalf(uint32_t addr, uint16_t value) const {
        const uint32_t off = HalfOffset("WriteHalf (misaligned)", addr, value);
        return MergeHalf(PeekReg(off & ~3u), off, value);
    }

    template <size_t N>
    void StoreResetValuesLocked(const Omap3530RegReset (&values)[N]) {
        for (const Omap3530RegReset& v : values) regs_[v.off / 4u] = v.value;
    }

    mutable std::mutex    mu_;
    std::vector<uint32_t> regs_;
};

inline uint32_t Omap3530PrcmStubBlock::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off & 3u) HaltUnsupportedAccess("ReadWord (misaligned)", addr, 0);
    std::lock_guard<std::mutex> lk(mu_);
    return regs_[off / 4u];
}

inline void Omap3530PrcmStubBlock::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off & 3u) HaltUnsupportedAccess("WriteWord (misaligned)", addr, value);
    std::lock_guard<std::mutex> lk(mu_);
    regs_[off / 4u] = value;
}

inline uint16_t Omap3530PrcmStubBlock::ReadHalf(uint32_t addr) {
    const uint32_t off = HalfOffset("ReadHalf (misaligned)", addr, 0);
    std::lock_guard<std::mutex> lk(mu_);
    return HalfOf(regs_[off / 4u], off);
}

inline void Omap3530PrcmStubBlock::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = HalfOffset("WriteHalf (misaligned)", addr, value);
    std::lock_guard<std::mutex> lk(mu_);
    regs_[off / 4u] = MergeHalf(regs_[off / 4u], off, value);
}

class Omap3530PrmDomainBlock : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    /* PM_PWSTST_<DOMAIN> at 0xE4 mirrors PM_PWSTCTRL_<DOMAIN> at 0xE0:
       the BSP writes the desired POWERSTATE in bits[1:0] of PWSTCTRL
       and polls PWSTST until POWERSTATEST==POWERSTATE. */
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0xE4u) {
            const uint32_t ctrl = PeekReg(0xE0u);
            return ctrl & 0x3u;
        }
        if (off == 0xE8u) {
            /* PM_PREPWSTST=0 makes OALContextRestore re-init
               GPIO/INTC, wiping driver register writes. */
            const uint32_t ctrl = PeekReg(0xE0u);
            return ctrl & 0x7u;
        }
        return Omap3530PrcmStubBlock::ReadWord(addr);
    }
};

class Omap3530GpioBankBase : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;
    uint32_t MmioSize() const override { return 0x00001000u; }

    /* 0..5 = GPIO1..GPIO6 (TRM nomenclature). */
    virtual uint32_t BankIndex() const = 0;

    /* OMAP3530 TRM Table 11-2: MPU_IRQ_29..34 = GPIO1..GPIO6. */
    virtual int IrqNumber() const = 0;

    /* SPRUF98Y section 4.5.2.2 (page 258): the cold reset and the global warm reset both reset the GPIO modules. */
    void OnReady() override {
        Omap3530PrcmStubBlock::OnReady();
        {
            std::lock_guard<std::mutex> lk(mu_);
            regs_[0x38u / 4u] = 0xFFFFFFFFu;
            LoadResetLocked();
        }
        emu_.Get<Omap3530GpioBus>().RegisterBank(BankIndex(), this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            std::lock_guard<std::mutex> lk(mu_);
            LoadResetLocked();
            RecomputeIrqLineLocked();
        });
    }

    void SetInputPin(uint32_t bit, bool high) {
        std::lock_guard<std::mutex> lk(mu_);
        const uint32_t mask = 1u << bit;
        uint32_t& datain = regs_[0x38 / 4u];
        const bool was_high = (datain & mask) != 0u;
        if (was_high == high) {
            RecomputeIrqLineLocked();
            return;
        }
        datain = high ? (datain | mask) : (datain & ~mask);
        const uint32_t risedet = regs_[0x48 / 4u];
        const uint32_t falldet = regs_[0x4C / 4u];
        const bool edge_match =
            (high  && (risedet & mask)) ||
            (!high && (falldet & mask));
        if (edge_match) {
            regs_[0x18 / 4u] |= mask;
        }
        RecomputeIrqLineLocked();
    }

    /* SPRUF98Y Table 24-27 (page 3392): GPIO_OE 0 configures the port as an output. */
    std::optional<bool> DrivenLevel(uint32_t bit) {
        std::lock_guard<std::mutex> lk(mu_);
        const uint32_t mask = 1u << bit;
        if ((regs_[0x34u / 4u] & mask) != 0u) return std::nullopt;
        return (regs_[0x3Cu / 4u] & mask) != 0u;
    }

    /* SPRUF98Y section 24.6.1 (page 3383): "All module registers are 8-, 16-, or 32-bit accessible". */
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off  = HalfOffset("WriteHalf (misaligned)", addr, value);
        const uint32_t word = off & ~3u;
        std::lock_guard<std::mutex> lk(mu_);
        WriteWordLocked(word, ActsOnOnes(word) ? static_cast<uint32_t>(value) << LaneShift(off)
                                               : MergeHalf(regs_[word / 4u], off, value));
    }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == 0x18u) {
            return IrqStatus1Computed();
        }
        return Omap3530PrcmStubBlock::ReadWord(addr);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off & 3u) HaltUnsupportedAccess("WriteWord (misaligned)",
                                            addr, value);
        std::lock_guard<std::mutex> lk(mu_);
        WriteWordLocked(off, value);
    }

private:
    void WriteWordLocked(uint32_t off, uint32_t value) {
        bool need_recompute = false;
        switch (off) {
        case 0x18u:
            regs_[0x18 / 4u] &= ~value;
            need_recompute = true;
            break;
        case 0x60u:
            regs_[0x1C / 4u] &= ~value;
            need_recompute = true;
            break;
        case 0x64u:
            regs_[0x1C / 4u] |= value;
            need_recompute = true;
            break;
        case 0x1Cu:
        case 0x40u:
        case 0x44u:
        case 0x48u:
        case 0x4Cu:
            regs_[off / 4u] = value;
            need_recompute = true;
            break;
        /* SPRUF98Y Table 24-57 (page 3401): CLEARDATAOUT clears, and Table 24-59 (page 3402): SETDATAOUT sets, the
           GPIO_DATAOUT bits written 1. */
        case 0x90u:
            regs_[0x3C / 4u] &= ~value;
            regs_[off / 4u] = value;
            break;
        case 0x94u:
            regs_[0x3C / 4u] |= value;
            regs_[off / 4u] = value;
            break;
        default:
            regs_[off / 4u] = value;
            break;
        }
        if (need_recompute) RecomputeIrqLineLocked();
    }

public:
    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mu_);
        SaveRegsLocked(w);
        w.Write("irq_line_high", irq_line_high_);
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mu_);
        RestoreRegsLocked(r);
        r.Read("irq_line_high", irq_line_high_);
    }

protected:
    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "GPIO_REVISION";
        case 0x10: return "GPIO_SYSCONFIG";
        case 0x14: return "GPIO_SYSSTATUS";
        case 0x18: return "GPIO_IRQSTATUS1";
        case 0x1C: return "GPIO_IRQENABLE1";
        case 0x20: return "GPIO_WAKEUPENABLE";
        case 0x28: return "GPIO_IRQSTATUS2";
        case 0x2C: return "GPIO_IRQENABLE2";
        case 0x30: return "GPIO_CTRL";
        case 0x34: return "GPIO_OE";
        case 0x38: return "GPIO_DATAIN";
        case 0x3C: return "GPIO_DATAOUT";
        case 0x40: return "GPIO_LEVELDETECT0";
        case 0x44: return "GPIO_LEVELDETECT1";
        case 0x48: return "GPIO_RISINGDETECT";
        case 0x4C: return "GPIO_FALLINGDETECT";
        case 0x50: return "GPIO_DEBOUNCENABLE";
        case 0x54: return "GPIO_DEBOUNCINGTIME";
        case 0x60: return "GPIO_CLEARIRQENABLE1";
        case 0x64: return "GPIO_SETIRQENABLE1";
        case 0x70: return "GPIO_CLEARIRQENABLE2";
        case 0x74: return "GPIO_SETIRQENABLE2";
        case 0x80: return "GPIO_CLEARWAKEUPENA";
        case 0x84: return "GPIO_SETWAKEUPENA";
        case 0x90: return "GPIO_CLEARDATAOUT";
        case 0x94: return "GPIO_SETDATAOUT";
        }
        return nullptr;
    }

private:
    /* SPRUF98Y Table 24-15 (page 3387): IRQSTATUS1; Table 24-45 and Table 24-47 (page 3398): CLEARIRQENABLE1 and
       SETIRQENABLE1; Table 24-57 (page 3401) and Table 24-59 (page 3402): CLEARDATAOUT and SETDATAOUT - each acts on
       the bits written 1. */
    static bool ActsOnOnes(uint32_t word) {
        return word == 0x18u || word == 0x60u || word == 0x64u || word == 0x90u || word == 0x94u;
    }

    /* SPRUF98Y pages 3385-3402: GPIO_CTRL resets to 0x2 (page 3391), GPIO_OE to 0xFFFFFFFF (Table 24-27, page 3392),
       every other writable register to 0; GPIO_SYSSTATUS RESETDONE reads 1, "Reset completed" (page 3387). */
    void LoadResetLocked() {
        const uint32_t datain = regs_[0x38u / 4u];
        std::fill(regs_.begin(), regs_.end(), 0u);
        regs_[0x14u / 4u] = 0x00000001u;
        regs_[0x30u / 4u] = 0x00000002u;
        regs_[0x34u / 4u] = 0xFFFFFFFFu;
        regs_[0x38u / 4u] = datain;
    }

    uint32_t IrqStatus1Computed() const {
        std::lock_guard<std::mutex> lk(mu_);
        const uint32_t datain = regs_[0x38 / 4u];
        const uint32_t lvl0   = regs_[0x40 / 4u];
        const uint32_t lvl1   = regs_[0x44 / 4u];
        const uint32_t level_pending = (lvl0 & ~datain) | (lvl1 & datain);
        return regs_[0x18 / 4u] | level_pending;
    }

    /* Caller holds mu_. */
    void RecomputeIrqLineLocked() {
        const uint32_t datain = regs_[0x38 / 4u];
        const uint32_t lvl0   = regs_[0x40 / 4u];
        const uint32_t lvl1   = regs_[0x44 / 4u];
        const uint32_t level_pending = (lvl0 & ~datain) | (lvl1 & datain);
        const uint32_t status = regs_[0x18 / 4u] | level_pending;
        const uint32_t enable = regs_[0x1C / 4u];
        const bool want_high = (status & enable) != 0u;
        if (want_high == irq_line_high_) return;
        irq_line_high_ = want_high;
        auto& intc = emu_.Get<IrqController>();
        if (want_high) intc.AssertIrq  (IrqNumber());
        else           intc.DeAssertIrq(IrqNumber());
    }

    bool irq_line_high_ = false;
};
