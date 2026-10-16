#include "../freescale_wdog_impl.h"

#include <cerfcore/core/log.h>
#include "../irq_controller.h"
#include "imx51_clock_input.h"
#include "imx51_id.h"
#include "imx51_iomuxc.h"

namespace {

using cerf_freescale_wdog_detail::FreescaleWdogBase;
using cerf_freescale_wdog_detail::kWcrReset;
using cerf_freescale_wdog_detail::kWcrWda;
using cerf_freescale_wdog_detail::kWcrWdbg;
using cerf_freescale_wdog_detail::kWcrWde;
using cerf_freescale_wdog_detail::kWcrWdzst;
using cerf_freescale_wdog_detail::kWicr;
using cerf_freescale_wdog_detail::kWmcr;
using cerf_freescale_wdog_detail::WdogResetSource;

/* MCIMX51RM Figure 62-3 / Table 62-5: bit 6 reserved; WDZST, WDBG and WDW lock at the
   first WCR write; WDE and WDT are write-one-once; WDT is reset only by POR. */
constexpr uint16_t kWcrWdw          = 1u << 7;
constexpr uint16_t kWcrWdt          = 1u << 3;
constexpr uint16_t kWcrWritable     = 0xFFBFu;
constexpr uint16_t kWcrWriteOnce    = kWcrWdw | kWcrWdbg | kWcrWdzst;
constexpr uint16_t kWcrWriteOneOnce = kWcrWde | kWcrWdt;

/* MCIMX51RM Table 62-7. */
constexpr uint16_t kWrsrSftw = 1u << 0;
constexpr uint16_t kWrsrTout = 1u << 1;

/* MCIMX51RM Figure 62-6 / Table 62-8: WIE and WICT write-once, WTIS w1c. */
constexpr uint16_t kWicrWie   = 1u << 15;
constexpr uint16_t kWicrWtis  = 1u << 14;
constexpr uint16_t kWicrWict  = 0x00FFu;
constexpr uint16_t kWicrReset = 0x0004u;

/* MCIMX51RM Table 3-2: IRQ 58 WDOG1. */
constexpr int kWdog1Irq = 58;

/* MCIMX51RM Figure 62-7 / Table 62-9. */
constexpr uint16_t kWmcrPde   = 1u << 0;
constexpr uint16_t kWmcrReset = kWmcrPde;

constexpr uint32_t kPowerDownSeconds = 16u;

class Imx51Wdog1 : public FreescaleWdogBase<0x73F98000u, SocId::Imx51> {
public:
    using FreescaleWdogBase::FreescaleWdogBase;

    void OnReady() override {
        iomuxc_         = &emu_.Get<Imx51Iomuxc>();
        power_down_event_ = emu_.Get<GuestCycleClock>().Add([this] { OnPowerDown(); });
        irq_event_        = emu_.Get<GuestCycleClock>().Add([this] { OnInterruptEvent(); });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { DrivePin(); });
        FreescaleWdogBase::OnReady();
    }

protected:
    uint64_t CkilHz() const override { return emu_.Get<Imx51ClockInput>().CkilHz(); }
    uint16_t WcrWritable() const override { return kWcrWritable; }
    uint16_t WcrWriteOnce() const override { return kWcrWriteOnce; }
    uint16_t WcrWriteOneOnce() const override { return kWcrWriteOneOnce; }

    bool SuspendedIn(FreescaleLowPowerMode mode) const override {
        switch (mode) {
            case FreescaleLowPowerMode::kRun:  return false;
            case FreescaleLowPowerMode::kWait: return (wcr_ & kWcrWdw) != 0u;
            case FreescaleLowPowerMode::kStop: return (wcr_ & kWcrWdzst) != 0u;
            default:                           break;
        }
        emu_.Get<Fatal>().Die("Imx51Wdog1: low-power mode %u is not an i.MX51 mode",
                              static_cast<unsigned>(mode));
    }

    /* MCIMX51RM Figure 62-1. */
    bool SuspendStopsPrescaler() const override { return true; }

    void OnWcrChange() override { DrivePin(); }

    /* MCIMX51RM 62.6.1.1 / Figure 62-10: a time-out resets the system and, with WDT set,
       asserts ipp_wdog until POR (62.6.6). */
    void OnCounterTimeout() override {
        if ((wcr_ & kWcrWdt) != 0u) {
            timeout_pin_ = true;
            DrivePin();
        }
        RaiseReset(WdogResetSource::kTimeout);
    }

    void OnPowerOn(uint64_t now) override {
        wcr_         = kWcrReset;
        wsr_         = 0u;
        wrsr_        = 0u;
        timeout_pin_ = false;
        RestartUnit(now);
        DrivePin();
    }

    /* MCIMX51RM 62.6.2: a system reset restores every register but WRSR and WDT; WDT is
       reset only by POR. Table 62-7: WRSR names the last WDOG-generated reset. */
    void ResetRegisters(ResetLineKind kind, WdogResetSource source, uint64_t now) override {
        const bool por = kind == ResetLineKind::Rtc;
        wcr_ = static_cast<uint16_t>(kWcrReset | (por ? 0u : (wcr_ & kWcrWdt)));
        wsr_ = 0u;
        if (por) {
            wrsr_        = 0u;
            timeout_pin_ = false;
        } else if (kind == ResetLineKind::Watchdog) {
            wrsr_ = source == WdogResetSource::kTimeout ? kWrsrTout : kWrsrSftw;
        }
        RestartUnit(now);
    }

    bool ReadExtra(uint32_t off, uint16_t& value) override {
        if (off == kWicr) { value = wicr_; return true; }
        if (off == kWmcr) { value = wmcr_; return true; }
        return false;
    }

    bool WriteExtra(uint32_t off, uint16_t value) override {
        if (off == kWicr) { WriteWicr(value); return true; }
        if (off == kWmcr) { WriteWmcr(value); return true; }
        return false;
    }

    void RearmExtra(uint64_t now) override {
        if (power_down_running_) {
            clock_->Arm(power_down_event_, CycleOfCount(power_down_tick_, now));
        } else {
            clock_->Disarm(power_down_event_);
        }
        RearmInterrupt(now);
    }

    void OnLoad(uint64_t now) override { ArmInterruptPoint(now); }

    void SaveExtra(StateWriter& w) override {
        w.Write("wicr", wicr_);
        w.Write("wmcr", wmcr_);
        w.Write<uint8_t>("wicr_written", wicr_written_ ? 1u : 0u);
        w.Write<uint8_t>("power_down_running", power_down_running_ ? 1u : 0u);
        w.Write<uint8_t>("power_down_pin", power_down_pin_ ? 1u : 0u);
        w.Write<uint8_t>("timeout_pin", timeout_pin_ ? 1u : 0u);
        w.Write("power_down_tick", power_down_tick_);
        w.Write<uint8_t>("irq_pending", irq_pending_ ? 1u : 0u);
    }

    void RestoreExtra(StateReader& r) override {
        uint8_t written = 0, running = 0, pd_pin = 0, to_pin = 0, irq = 0;
        r.Read("wicr", wicr_);
        r.Read("wmcr", wmcr_);
        r.Read("wicr_written", written);
        r.Read("power_down_running", running);
        r.Read("power_down_pin", pd_pin);
        r.Read("timeout_pin", to_pin);
        r.Read("power_down_tick", power_down_tick_);
        r.Read("irq_pending", irq);
        wicr_written_       = written != 0u;
        power_down_running_ = running != 0u;
        power_down_pin_     = pd_pin != 0u;
        timeout_pin_        = to_pin != 0u;
        irq_pending_        = irq != 0u;
        clock_->Disarm(power_down_event_);
        clock_->Disarm(irq_event_);
    }

    void PostRestoreExtra() override {
        DrivePin();
        DriveInterrupt();
    }

private:
    void RestartUnit(uint64_t now) {
        wicr_               = kWicrReset;
        wicr_written_       = false;
        wmcr_               = kWmcrReset;
        power_down_pin_     = false;
        power_down_tick_    = CountAt(now) +
                              static_cast<uint32_t>(kPowerDownSeconds * CkilHz());
        power_down_running_ = true;
        clock_->Arm(power_down_event_, CycleOfCount(power_down_tick_, now));
        irq_pending_ = false;
        clock_->Disarm(irq_event_);
        DriveInterrupt();
    }

    void WriteWicr(uint16_t value) {
        if ((value & kWicrWtis) != 0u) wicr_ = static_cast<uint16_t>(wicr_ & ~kWicrWtis);
        if (!wicr_written_) {
            wicr_ = static_cast<uint16_t>((wicr_ & kWicrWtis) |
                                          (value & (kWicrWie | kWicrWict)));
            wicr_written_ = true;
            if (counting_) ArmInterruptPoint(clock_->Cycles());
        }
        DriveInterrupt();
    }

    /* MCIMX51RM §62.6.3.4 (page 62-11): "WDOG generates an interrupt before timing out if the WIE
       bit is set in WICR", WICT x 0.5 s before time-out; a service before it reloads the counter,
       "and an interrupt will not be triggered". */
    void ArmInterruptPoint(uint64_t now) {
        const uint32_t ahead = static_cast<uint32_t>(wicr_ & kWicrWict) *
                               cerf_freescale_wdog_detail::kCkilPerCount;
        const uint32_t point = timeout_tick_ - ahead;
        irq_pending_ = (wicr_ & kWicrWie) != 0u &&
                       static_cast<int32_t>(point - CountAt(now)) > 0;
        RearmInterrupt(now);
    }

    void RearmInterrupt(uint64_t now) {
        if (!irq_pending_ || !counting_ || Suspended()) {
            clock_->Disarm(irq_event_);
            return;
        }
        const uint32_t ahead = static_cast<uint32_t>(wicr_ & kWicrWict) *
                               cerf_freescale_wdog_detail::kCkilPerCount;
        clock_->Arm(irq_event_, CycleOfCount(timeout_tick_ - ahead, now));
    }

    /* MCIMX51RM §62.6.1.2 (page 62-10): "The interrupt will be deasserted only if software
       clears the WTIS bit in WICR Register". */
    void OnInterruptEvent() {
        if (!counting_) return;
        irq_pending_ = false;
        wicr_ = static_cast<uint16_t>(wicr_ | kWicrWtis);
        DriveInterrupt();
    }

    void DriveInterrupt() {
        auto& irq = emu_.Get<IrqController>();
        if ((wicr_ & kWicrWtis) != 0u && (wicr_ & kWicrWie) != 0u) irq.AssertIrq(kWdog1Irq);
        else                                                       irq.DeAssertIrq(kWdog1Irq);
    }

    /* MCIMX51RM Table 62-9: once PDE is cleared the counter cannot be enabled again. */
    void WriteWmcr(uint16_t value) {
        if ((value & kWmcrPde) != 0u) return;
        LOG(SocWdt, "Imx51Wdog1: WMCR PDE cleared (power-down counter %s)\n",
            power_down_running_ ? "running" : "stopped");
        wmcr_ = 0u;
        if (clock_->IsDue(power_down_event_, clock_->Cycles())) return;
        power_down_running_ = false;
        clock_->Disarm(power_down_event_);
    }

    /* MCIMX51RM 62.6.1.3 / Figure 62-11. */
    void OnPowerDown() {
        if (!power_down_running_) {
            emu_.Get<Fatal>().Die("Imx51Wdog1: power-down event fired with the counter "
                                  "disabled");
        }
        power_down_running_ = false;
        power_down_pin_     = true;
        LOG(SocWdt, "Imx51Wdog1: power-down counter expired, WDOG_B asserted\n");
        DrivePin();
    }

    /* MCIMX51RM 62.6.6 / Figure 62-8: ipp_wdog follows WDA, the power-down counter and a
       WDT time-out. */
    void DrivePin() {
        iomuxc_->DriveWdog1WdogB((wcr_ & kWcrWda) == 0u || power_down_pin_ || timeout_pin_);
    }

    Imx51Iomuxc*            iomuxc_             = nullptr;
    GuestCycleClock::Event* power_down_event_   = nullptr;
    GuestCycleClock::Event* irq_event_          = nullptr;
    bool                    irq_pending_        = false;
    uint16_t                wicr_               = kWicrReset;
    uint16_t                wmcr_               = kWmcrReset;
    bool                    wicr_written_       = false;
    bool                    power_down_running_ = false;
    bool                    power_down_pin_     = false;
    bool                    timeout_pin_        = false;
    uint32_t                power_down_tick_    = 0u;
};

}

REGISTER_SERVICE(Imx51Wdog1);
