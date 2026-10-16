#pragma once

#include "vr41xx_bcu_boot_write.h"
#include "vr41xx_bcu_regs.h"
#include "vr41xx_reg_window_impl.h"

#include <cerfcore/core/fatal.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>
#include <vector>

namespace cerf_vr41xx_reg_window_detail {

constexpr bool WritableAndWrite0Disjoint(const Vr41xxRegWindowModel& m) {
    for (uint32_t i = 0; i < m.num_regs; ++i) {
        if ((m.reg[i].wmask & m.reg[i].fatal_on_set) != 0u) return false;
    }
    return true;
}

constexpr bool WritableAndWrite0Partition(const Vr41xxRegWindowModel& m, uint32_t off) {
    const Vr41xxRegSpec& r = m.reg[off / 2u];
    return (r.wmask & r.fatal_on_set) == 0u && (r.wmask | r.fatal_on_set) == 0xFFFFu;
}

template <const std::string_view& Soc, Vr41xxRegWindowModel M>
class Vr41xxBcuBase : public Vr41xxRegWindowBase<Soc, M> {
    using Base = Vr41xxRegWindowBase<Soc, M>;

    static constexpr uint32_t kCnt1Slot = vr41xx_bcu::kOffCnt1 / 2u;
    static constexpr uint32_t kCnt2Slot = vr41xx_bcu::kOffCnt2 / 2u;

    static_assert(M.reg[kCnt1Slot].read == ReadKind::kStored &&
                  M.reg[kCnt1Slot].write == WriteKind::kStored &&
                  (M.reg[kCnt1Slot].wmask & vr41xx_bcu::kCnt1IsamLcd) != 0u &&
                  M.reg[kCnt2Slot].read == ReadKind::kStored &&
                  M.reg[kCnt2Slot].write == WriteKind::kStored &&
                  M.reg[kCnt2Slot].wmask == vr41xx_bcu::kCnt2Gmode,
                  "VR41xx BCU model must store BCUCNTREG1 ISAM/LCD and BCUCNTREG2 GMODE");
    static_assert(WritableAndWrite0Disjoint(M),
                  "VR41xx BCU fatal_on_set bits are write-0 RFU bits, never writable bits");

public:
    using Base::Base;

    void OnReady() override {
        Base::OnReady();
        ApplyEntryWrites();
        dispatcher_   = &this->emu_.template Get<PeripheralDispatcher>();
        lcd_space_id_ = dispatcher_->InstallDataInversion(vr41xx_bcu::kLcdSpaceBase,
                                                          vr41xx_bcu::kLcdSpaceEnd);
        UpdateLcdInversion();
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - M.base;
        if (const char* why = EncodingRejected(off, value)) {
            this->HaltUnsupportedAccess(why, addr, value);
        }
        Base::WriteHalf(addr, value);
        if (off == vr41xx_bcu::kOffCnt1 || off == vr41xx_bcu::kOffCnt2) UpdateLcdInversion();
    }

    void RestoreState(StateReader& r) override {
        Base::RestoreState(r);
        UpdateLcdInversion();
    }

protected:
    const char* RegStateName() const override { return "bcu_reg"; }

    virtual std::vector<Vr41xxBcuBootWrite> KernelEntryWrites() const { return {}; }

    virtual const char* EncodingRejected(uint32_t off, uint16_t value) const {
        (void)off;
        (void)value;
        return nullptr;
    }

    void AfterReset() override {
        ApplyEntryWrites();
        UpdateLcdInversion();
    }

private:
    void ApplyEntryWrites() {
        for (const Vr41xxBcuBootWrite& w : KernelEntryWrites()) {
            const uint32_t i = w.offset / 2u;
            if (i < M.num_regs && M.reg[i].read == ReadKind::kFatal &&
                M.reg[i].write == WriteKind::kFatal) {
                continue;
            }
            if (const char* why = EncodingRejected(w.offset, w.value)) {
                this->emu_.template Get<Fatal>().Die(
                    "%s: kernel-entry %s (0x%04X at 0x%02X)", typeid(*this).name(), why,
                    w.value, w.offset);
            }
            this->ApplyStoredWrite(w.offset, w.value);
        }
    }

    void UpdateLcdInversion() {
        dispatcher_->SetDataInversion(
            lcd_space_id_, (this->StoredReg(kCnt1Slot) & vr41xx_bcu::kCnt1IsamLcd) == 0u &&
                               (this->StoredReg(kCnt2Slot) & vr41xx_bcu::kCnt2Gmode) == 0u);
    }

    PeripheralDispatcher*                  dispatcher_   = nullptr;
    PeripheralDispatcher::DataInversionId lcd_space_id_ = 0u;
};

}
