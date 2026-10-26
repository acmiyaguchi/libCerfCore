#pragma once

#include <cerfcore/state/state_stream.h>
#include "imx28_apb_peripheral.h"
#include "imx28_power.h"
#include "imx28_usb_endpoints.h"
#include "imx28_usbctrl_regs.h"

#include <cstdint>
#include <functional>
#include <mutex>

namespace cerfcore {

class Imx28Digctl;
class Imx28Icoll;

/* MCIMX28RM Rev. 2 chapter 31 (USBCTRL), one instance per unit. */
class Imx28Usbctrl : public Imx28ApbPeripheral {
public:
    Imx28Usbctrl(CerfEmulator& emu, uint32_t unit, uint32_t base)
        : Imx28ApbPeripheral(emu), base_(base), unit_(unit), endpoints_(emu, base) {}

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return base_; }
    uint32_t MmioSize() const override { return 0x00010000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     SaveState(StateWriter& w) override;
    void     RestoreState(StateReader& r) override;
    void     PostRestore() override { DriveLine(true); }

    void     AttachHost(std::function<void()> on_link, std::function<void()> on_prime);
    bool     Linked();
    void     BusReset();
    void     BusResetEnd();
    void     Setup(const uint8_t* setup);
    bool     Transact(uint32_t bit, uint8_t* data, uint32_t length, Imx28UsbEndpoints::Transfer& out);
    uint32_t DeviceAddress();

protected:
    bool HasSct() const override { return false; }

private:
    void     Reset();
    void     ResetLocked();
    uint32_t Mode() const { return r_[kMode] & kUsbModeCmMask; }
    uint32_t ComposeSts() const;
    uint32_t ComposeOtgsc();
    uint32_t ComposePortsc() const;
    uint32_t ReadReg(uint32_t off, uint32_t addr);
    void     WriteReg(uint32_t off, uint32_t value, uint32_t addr);
    void     WriteGptCtrl(uint32_t i, uint32_t value, uint32_t addr);
    void     WriteCmd(uint32_t value, uint32_t addr);
    void     WriteHostCmd(uint32_t value, uint32_t addr);
    uint32_t ComposeFrIndex() const;
    void     WriteFrIndex(uint32_t value, uint32_t addr);
    void     WriteMode(uint32_t value, uint32_t addr);
    void     WritePortsc(uint32_t value, uint32_t addr);
    void     WriteOtgsc(uint32_t value, uint32_t addr);
    void     Prime(uint32_t bits, uint32_t addr, uint32_t value);
    void     Raise(uint32_t sts);
    void     NotifyLink();
    void     OnSessionChange();
    void     DriveLine(bool force);
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    uint32_t base_;
    uint32_t unit_;
    mutable std::mutex mtx_;
    Imx28Icoll*  icoll_  = nullptr;
    Imx28Digctl* digctl_ = nullptr;
    Imx28Power*  power_  = nullptr;
    Imx28Power::UsbSession session_{};
    Imx28UsbEndpoints      endpoints_;
    std::function<void()>  on_link_;
    std::function<void()>  on_prime_;

    uint32_t gptld_[2]   = {};
    uint32_t gptctrl_[2] = {};
    uint32_t gptcnt_[2]  = {};
    uint32_t endptctrl_[kEndpoints] = {};
    uint32_t r_[21] = {};
    uint32_t port_ = 0;
    int64_t  run_ns_ = 0;
    int64_t  onems_ack_ = 0;
    bool     mode_locked_ = false;
    bool     line_ = false;

    enum RegSlot : uint32_t {
        kSbusCfg, kCmd, kSts, kIntr, kFrIndex, kAddrBase, kListAddr, kTtCtrl, kBurst, kTxFill,
        kIcUsb, kNak, kNakEn, kPortsc, kOtgsc, kMode, kDciVersion, kSetupStat, kComplete, kEndptStat,
        kConfigFlag
    };
};

}
