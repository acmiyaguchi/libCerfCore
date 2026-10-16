#pragma once

#include "sa1111_unit.h"

#include <cstdint>
#include <functional>
#include <vector>

class Sa1111Sbi : public Sa1111Unit {
public:
    using Sa1111Unit::Sa1111Unit;

    enum class Mbgnt    { Arbiter, Low, High, Undetermined };
    enum class BusGrant { Granted, Stalled };

    bool ShouldRegister() override;

    uint32_t MmioBase() const override { return 0x40000000u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void SetHostPins(std::function<Mbgnt()> mbgnt);
    void OnHostPinsChange();
    void RegisterGrantListener(std::function<void()> fn);

    uint32_t Skcr() const { return skcr_; }
    bool BusClocksEnabled() const { return (skcr_ & kSkcrRclkEn) != 0u; }
    bool SleepRequested() const { return asleep_; }
    BusGrant Grant() const;
    void WakeOnAccess();

protected:
    void     OnChipReset(bool held) override;
    /* SA-1111 Developer's Manual §2.4.2 (page 2-5): "SBI registers, on the other hand, are
       latch-based and can be written without RCLK enabled." */
    bool     NeedsRclk(uint32_t) const override { return false; }
    uint32_t UnitReadWord (uint32_t addr) override;
    void     UnitWriteWord(uint32_t addr, uint32_t value) override;

private:
    void NotifyGrant();
    void EnterSleep();

    std::function<Mbgnt()> mbgnt_;
    std::vector<std::function<void()>> grant_listeners_;

    /* SA-1111 Developer's Manual Table 3-9 (page 3-15): RCLKEn bit 1 "(RCLK and DCLK)", Sleep bit 2
       and Doze bit 3 "WO", "1 = Enter sleep mode" / "1 = Enter doze mode", "0 = No effect.",
       VCO_OFF bit 4 "1= Off". */
    static constexpr uint32_t kSkcrRclkEn    = 1u << 1;
    static constexpr uint32_t kSkcrSleep     = 1u << 2;
    static constexpr uint32_t kSkcrDoze      = 1u << 3;
    static constexpr uint32_t kSkcrVcoOff    = 1u << 4;

    static constexpr uint32_t kSkcrReset = 0x80u;   /* RdyEn reset (Table 3-9). */
    static constexpr uint32_t kSmcrReset = 0x35u;   /* DTIM|DRAC=5|CLAT reset (Table 3-10). */
    /* Table 3-10: MBGE bit 1 "1=MBGNT is enabled". */
    static constexpr uint32_t kSmcrMbge = 1u << 1;

    /* Table 3-3 note 1: "All reserved bits are read back as zero." Table 3-9 SKCR bits
       12:0; Table 3-10 SMCR bits 5:0. */
    static constexpr uint32_t kSkcrDefined = 0x1FFFu;
    static constexpr uint32_t kSmcrDefined = 0x3Fu;

    uint32_t skcr_   = kSkcrReset;
    uint32_t smcr_   = kSmcrReset;
    bool     asleep_ = false;
};
