#pragma once

#include <cerfcore/state/state_stream.h>
#include "imx28_apb_peripheral.h"
#include "imx28_power_regs.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cerfcore {

class GuestDeepSleep;
class Imx28Icoll;
class Imx28LradcInputs;

/* MCIMX28RM Rev. 2 chapter 11 (POWER) at 0x8004_4000. */
class Imx28Power : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    struct UsbSession {
        bool avalid;
        bool bvalid;
        bool vbusvalid;
        bool sessend;
        bool operator==(const UsbSession&) const = default;
    };

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80044000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* Section 38.5.20 AUTOMATIC: the LRADC loads the scaled battery voltage into BATT_VAL. */
    void LoadBattVal(uint32_t scaled);

    /* Sections 11.12.13 STS [3:0] for USB instance 0 and 11.12.19 USB1CTRL for instance 1. */
    UsbSession Session(uint32_t unit);
    void       RegisterSessionListener(uint32_t unit, std::function<void()> fn) {
        session_listeners_[unit].push_back(std::move(fn));
    }

    /* Section 11.4 Table 11-1: HW_POWER_STS_PSWITCH level of the pin, 0x0 low, 0x1 mid, 0x3 high. */
    void SetPswitchLevel(uint32_t level);

    /* Section 11.12.16 RESET.PWD: the sleep in progress began with the PWD write. */
    bool SleptByPwd() const { return pwd_sleep_.load(std::memory_order_acquire); }

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    struct Analog {
        uint32_t vdd5v_mv;
        uint32_t batt_mv;
        uint32_t vddio_mv;
    };
    struct Signals {
        bool vdd5v_gt_vddio;
        bool vbusvalid;
        bool bvalid;
        bool avalid;
        bool sessend;
        bool droop;
        bool batt_bo;
    };

    Analog   SampleAnalog();
    Signals  ComputeSignals(const Analog& a) const;
    uint32_t ComposeSts();
    uint32_t Vdd4p2Millivolts(const Analog& a) const;
    UsbSession SessionOf(uint32_t unit, const Signals& s) const;
    void     Reset();
    void     Refresh();
    void     LatchInterrupts(const Signals& s);
    void     DriveLines(bool force);
    void     WriteReg(uint32_t index, Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteReset(Imx28SctOp op, uint32_t value);
    bool     PowerUpEvent();
    void     EnterPoweredDown();
    bool     PowerUpDue();
    int64_t  PowerUpDueNs();

    mutable std::mutex mtx_;
    Imx28Icoll*       icoll_  = nullptr;
    Imx28LradcInputs* inputs_ = nullptr;
    GuestDeepSleep*   sleep_  = nullptr;
    uint32_t regs_[kPwrRegCount] = {};
    uint32_t lines_ = 0u;
    UsbSession sessions_[2] = {};
    bool       vdd5v_gt_vddio_ = false;
    uint32_t   pswitch_ = 0u;
    bool       powered_down_   = false;
    std::atomic<bool> pwd_write_{false};
    std::atomic<bool> pwd_sleep_{false};
    int64_t    event_since_ns_ = -1;
    std::vector<std::function<void()>> session_listeners_[2];
};

}
