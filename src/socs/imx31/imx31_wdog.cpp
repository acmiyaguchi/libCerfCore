#include "../freescale_wdog_impl.h"

#include "imx31_ccm.h"
#include "imx31_id.h"
#include "imx31_wdog_pin.h"

namespace cerfcore {

namespace {

using cerf_freescale_wdog_detail::FreescaleWdogBase;
using cerf_freescale_wdog_detail::kWcrReset;
using cerf_freescale_wdog_detail::kWcrWda;
using cerf_freescale_wdog_detail::kWcrWdbg;
using cerf_freescale_wdog_detail::kWcrWde;
using cerf_freescale_wdog_detail::kWcrWdzst;
using cerf_freescale_wdog_detail::WdogResetSource;

/* MCIMX31RM Table 37-6: bit 7 reserved; WRE, WDBG and WDZST write once-only; WDE can
   only be written to 1. */
constexpr uint16_t kWcrWoe       = 1u << 6;
constexpr uint16_t kWcrWre       = 1u << 3;
constexpr uint16_t kWcrWritable  = 0xFF7Fu;
constexpr uint16_t kWcrWriteOnce = kWcrWre | kWcrWdbg | kWcrWdzst;

/* MCIMX31RM Table 37-8. */
constexpr uint16_t kWrsrSftw = 1u << 0;
constexpr uint16_t kWrsrTout = 1u << 1;
constexpr uint16_t kWrsrExt  = 1u << 3;
constexpr uint16_t kWrsrPwr  = 1u << 4;

class Imx31Wdog : public FreescaleWdogBase<0x53FDC000u, SocId::Imx31> {
public:
    using FreescaleWdogBase::FreescaleWdogBase;

    void OnReady() override {
        /* MCIMX31RM section 37.5.7 (page 37-11): "Both a system reset and a power-on reset can
           deassert it in between." */
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { DrivePin(); });
        FreescaleWdogBase::OnReady();
    }

protected:
    uint64_t CkilHz() const override { return emu_.Get<Imx31Ccm>().CkilHz(); }
    uint16_t WcrWritable() const override { return kWcrWritable; }
    uint16_t WcrWriteOnce() const override { return kWcrWriteOnce; }
    uint16_t WcrWriteOneOnce() const override { return kWcrWde; }

    bool SuspendedIn(FreescaleLowPowerMode mode) const override {
        return (wcr_ & kWcrWdzst) != 0u && mode != FreescaleLowPowerMode::kRun;
    }

    /* MCIMX31RM Figure 37-1. */
    bool SuspendStopsPrescaler() const override { return false; }

    void OnWcrChange() override { DrivePin(); }

    /* MCIMX31RM Figure 37-7 (page 37-14): with WRE set a time-out asserts the WDOG signal and
       the counter waits for a system reset. */
    void OnCounterTimeout() override {
        if ((wcr_ & kWcrWre) == 0u) {
            RaiseReset(WdogResetSource::kTimeout);
            return;
        }
        if ((wcr_ & kWcrWoe) == 0u || pin_asserted_) return;
        auto* pin = emu_.TryGet<Imx31WdogPin>();
        if (pin == nullptr) {
            emu_.Get<Fatal>().Die("Imx31Wdog: time-out with WCR 0x%04X asserts the WDOG signal; "
                                  "the pin is not modeled", wcr_);
        }
        pin->SetWdogAsserted(true);
        pin->SetWdogAsserted(false);
    }

    void OnPowerOn(uint64_t) override {
        wcr_          = kWcrReset;
        wsr_          = 0u;
        wrsr_         = kWrsrPwr;
        pin_asserted_ = false;
    }

    /* MCIMX31RM 37.5.2: a system reset restores every register but WRSR; Table 37-6 WOE
       survives a software reset only. Table 37-8: WRSR names the last reset source. */
    void ResetRegisters(ResetLineKind kind, WdogResetSource source, uint64_t) override {
        const uint16_t woe = source == WdogResetSource::kSoftware ? (wcr_ & kWcrWoe) : 0u;
        wcr_ = static_cast<uint16_t>(kWcrReset | woe);
        wsr_ = 0u;
        switch (kind) {
            case ResetLineKind::Rtc:   wrsr_ = kWrsrPwr; break;
            case ResetLineKind::Other: wrsr_ = kWrsrExt; break;
            case ResetLineKind::Watchdog:
                wrsr_ = source == WdogResetSource::kTimeout ? kWrsrTout : kWrsrSftw;
                break;
        }
    }

    void PostRestoreExtra() override {
        pin_asserted_ = PinLevel();
        SendPin();
    }

private:
    /* MCIMX31RM Table 37-6 (page 37-6): WOE "0 Pin is tri-stated 1 Pin configured as
       output"; WDA "0 Assert WDOG output". */
    bool PinLevel() const {
        return (wcr_ & kWcrWoe) != 0u && (wcr_ & kWcrWda) == 0u;
    }

    void DrivePin() {
        const bool level = PinLevel();
        if (level == pin_asserted_) return;
        pin_asserted_ = level;
        SendPin();
    }

    void SendPin() {
        if (auto* pin = emu_.TryGet<Imx31WdogPin>()) {
            pin->SetWdogAsserted(pin_asserted_);
            return;
        }
        if (pin_asserted_) {
            emu_.Get<Fatal>().Die("Imx31Wdog: WCR 0x%04X asserts the WDOG signal; the pin is "
                                  "not modeled", wcr_);
        }
    }

    bool pin_asserted_ = false;
};

}

REGISTER_SERVICE(Imx31Wdog);

}
