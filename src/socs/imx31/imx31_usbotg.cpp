#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../execution/guest_cycle_clock.h"
#include "imx31_avic.h"
#include "imx31_ccm.h"
#include "imx31_id.h"
#include "imx31_plls.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "../freescale_module_clocks.h"
#include "../freescale_timer_clocks.h"
#include "../freescale_usb_frame_index.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX31RM Ch 32 (Tables 32-23/24/32) USB-OTG, no USB device wired. PORTSC
   config bits (31:12) are R/W; status bits (11:0 - CCS/PE/etc.) must read 0 so
   the kernel's connect/enable checks see no device. Unreached registers halt. */
constexpr uint32_t kBase        = 0x43F88000u;
constexpr uint32_t kSize        = 0x00004000u;
constexpr uint32_t kPortscFirst = 0x184u;
constexpr uint32_t kPortscLast  = 0x1A0u;  /* PORTSC1..PORTSC8, 4-byte stride */
constexpr uint32_t kPortscCfg   = 0xFFFFF000u; /* bits 31:12 R/W config; 11:0 status */
constexpr uint32_t kPortscPtsShift = 30;
constexpr uint32_t kPtsUlpi        = 2u;
constexpr uint32_t kPortscPhcd     = 1u << 23;
constexpr uint32_t kUsbcmd      = 0x140u;
constexpr uint32_t kUsbsts      = 0x144u;
constexpr uint32_t kUsbintr     = 0x148u;
constexpr uint32_t kCmdRs       = 1u << 0;     /* USBCMD Run/Stop */
constexpr uint32_t kCmdRst      = 1u << 1;     /* USBCMD Controller Reset (self-clears) */
constexpr uint32_t kCmdPse      = 1u << 4;     /* USBCMD Periodic Schedule Enable (Table 32-23) */
constexpr uint32_t kCmdAse      = 1u << 5;     /* USBCMD Async Schedule Enable (Table 32-23) */
constexpr uint32_t kStsHcHalted = 1u << 12;    /* USBSTS HCHalted */
constexpr uint32_t kStsPs       = 1u << 14;    /* USBSTS Periodic Schedule Status (Table 32-24) */
constexpr uint32_t kStsAs       = 1u << 15;    /* USBSTS Async Schedule Status (Table 32-24) */
constexpr uint32_t kUsbcmdReset = 0x00080000u; /* ITC=0x08 (Table 32-23) */
constexpr uint32_t kUsbCtrl     = 0x600u;      /* USB_CTRL (Table 32-3) */
constexpr uint32_t kOtgMirror   = 0x604u;      /* OTG_MIRROR */
constexpr uint32_t kUsbmode     = 0x1A8u;      /* USBMODE CM/ES (Table 32-34) */
constexpr uint32_t kUsbmodeCm   = 0x3u;
constexpr uint32_t kUsbmodeHost = 0x3u;
constexpr uint32_t kUsbmodeDevice = 0x2u;
constexpr uint32_t kFrameStatusBits =
    FreescaleUsbFrameIndex::kStsSri | FreescaleUsbFrameIndex::kStsFri;
constexpr uint32_t kConfigFlag  = 0x180u;      /* CONFIGFLAG CF bit0, reset 1 (Table 32-13) */
constexpr uint32_t kOtgsc       = 0x1A4u;      /* OTGSC On-The-Go status/ctrl (Table 32-33) */
/* OTGSC: bits30:24 interrupt-enable + bits5:0 control are R/W; bits19:16 are
   W1C interrupt-status; bits14:8 are R/O OTG line status. No USB cable on the
   Zune (a B-device): ID(8)=1 (B-device), BSE(12)=1 (VBus below B-session-end),
   BSV/ASV/AVV=0 (Table 32-33). */
constexpr uint32_t kOtgscRwMask = 0x7F00003Bu;
constexpr uint32_t kOtgscNoCable = 0x00001100u;  /* ID(8) | BSE(12) */
/* Device-mode endpoint controller (Table 32-2) - used by the USB-function /
   MTP driver. All 32-bit, reset 0. */
constexpr uint32_t kEndptSetupStat = 0x1ACu;   /* ENDPTSETUPSTAT W1C (Table 32-35) */
constexpr uint32_t kEndptPrime     = 0x1B0u;   /* ENDPTPRIME, HW-clears (Table 32-36) */
constexpr uint32_t kEndptFlush     = 0x1B4u;   /* ENDPTFLUSH, HW-clears (Table 32-37) */
constexpr uint32_t kEndptStat      = 0x1B8u;   /* ENDPTSTAT R/O ready bitmap (Fig 32-41) */
constexpr uint32_t kEndptComplete  = 0x1BCu;   /* ENDPTCOMPLETE W1C */
constexpr uint32_t kEndptCtrl0     = 0x1C0u;   /* ENDPTCTRL0..15 (§32.9.5.18) */
constexpr uint32_t kEndptCtrl15    = 0x1FCu;
/* EHCI operational list/index regs (§32.9.5, Figs 32-27/28/30). */
constexpr uint32_t kFrIndex     = 0x14Cu;      /* FRINDEX */
constexpr uint32_t kCtrlDsSeg   = 0x150u;      /* CTRLDSSEGMENT 4G segment, unused (ADC=0) */
constexpr uint32_t kPeriodicBase = 0x154u;     /* PERIODICLISTBASE / DEVICEADDR */
constexpr uint32_t kAsyncAddr   = 0x158u;      /* ASYNCLISTADDR / ENDPOINTLISTADDR */
constexpr uint32_t kTxFillTune  = 0x164u;      /* TXFILLTUNING perf tuning, reset 0 (Fig 32-33) */
constexpr uint32_t kUlpiView    = 0x170u;      /* ULPIVIEW (Fig 32-34) */
constexpr uint32_t kUlpiWu      = 1u << 31;    /* ULPIVIEW Wakeup, self-clears */
constexpr uint32_t kUlpiRun     = 1u << 30;    /* ULPIVIEW Run, self-clears on xfer done */

/* EHCI capability registers (read-only) - MCIMX31RM §32.9.4 Fig 32-18..32-21.
   0x100 word packs CAPLENGTH(0x40)|HCIVERSION(0x0100<<16); HCSPARAMS N_PORTS=1
   (a 0 there is "undefined" so the host driver needs ≥1); HCCPARAMS=0x0006. */
constexpr uint32_t kCapLength   = 0x100u;
constexpr uint32_t kHciVersion  = 0x102u;
constexpr uint32_t kHcsParams   = 0x104u;
constexpr uint32_t kHccParams   = 0x108u;
constexpr uint32_t kCapWord     = 0x01000040u;
constexpr uint32_t kHcsParamsVal = 0x00000001u;
constexpr uint32_t kHccParamsVal = 0x00000006u;
/* MCIMX31RM Table 2-3: interrupt 37, source "USB", "OTG". */
constexpr uint32_t kAvicSourceUsbOtg = 37u;

class Imx31Usbotg : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }
    void OnReady() override {
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        timer_clocks_  = &emu_.Get<FreescaleTimerClocks>();
        plls_          = &emu_.Get<Imx31Plls>();
        clock_         = &emu_.Get<GuestCycleClock>();
        avic_          = &emu_.Get<Imx31Avic>();
        irq_event_     = clock_->Add([this] { ArmFrameIrq(); });
        frame_index_.Attach();
        frame_index_.SetClocked(0u, true);
        clock_->RegisterRateListener([this] {
            frame_index_.Rescale();
            ArmFrameIrq();
        });
        module_clocks_->RegisterGateListener([this] { OnUsbClockChange(); });
        emu_.Get<Imx31Ccm>().RegisterRateListener([this] { OnUsbClockChange(); });
        clock_->RegisterIdleListener([this] { ApplyFrameClock(timer_clocks_->WfiMode()); });
        clock_->RegisterIdleExitListener([this] { ApplyFrameClock(FreescaleLowPowerMode::kRun); });
        /* MCIMX31RM §3.6.1: "periph_reset_out signal is connected to all peripherals except
           EMI". */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ResetController();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    /* CAPLENGTH (8-bit) and HCIVERSION (16-bit) are read at their native
       widths by the host driver; the cap word is also word-readable above. */
    uint8_t ReadByte(uint32_t addr) override {
        if (addr - kBase == kCapLength) return 0x40u;     /* CAPLENGTH (Fig 32-18) */
        HaltUnsupportedAccess("ReadByte", addr, 0);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - kBase == kHciVersion) return 0x0100u;  /* HCIVERSION (Fig 32-19) */
        HaltUnsupportedAccess("ReadHalf", addr, 0);
    }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (IsPortsc(off)) return portsc_[(off - kPortscFirst) / 4];  /* config only; status=0 */
        if (off >= kEndptCtrl0 && off <= kEndptCtrl15)
            return endptctrl_[(off - kEndptCtrl0) / 4];
        switch (off) {
            case kCapLength: return kCapWord;
            case kHcsParams: return kHcsParamsVal;
            case kHccParams: return kHccParamsVal;
            case kUsbcmd:  return usbcmd_;
            case kUsbsts: {
                uint32_t sts = (usbcmd_ & kCmdRs) ? 0u : kStsHcHalted;
                if (usbcmd_ & kCmdAse) sts |= kStsAs;
                if (usbcmd_ & kCmdPse) sts |= kStsPs;
                return sts | frame_index_.Status(0u);
            }
            case kUsbintr: return usbintr_;
            /* USB_CTRL/OTG_MIRROR: PHY/control RMW config; no device/no wake so
               the wake-request status bits stay 0 (store-with-reset). */
            case kUsbCtrl:   return usb_ctrl_;
            case kOtgMirror: return otg_mirror_;
            case kUsbmode:   return usbmode_;  /* kernel sets CM=device */
            case kConfigFlag: return configflag_;  /* CF: ports routed to this HC */
            case kOtgsc:     return (otgsc_ & kOtgscRwMask) | kOtgscNoCable;
            case kUlpiView:  return ulpiview_;
            case kFrIndex:      return frame_index_.Frindex(0u);
            case kCtrlDsSeg:    return ctrl_ds_seg_;
            case kPeriodicBase: return periodic_base_;
            case kAsyncAddr:    return async_addr_;
            case kTxFillTune:   return tx_fill_tune_;
            case kEndptSetupStat: return 0;
            case kEndptPrime:     return endpt_prime_;
            case kEndptFlush:     return endpt_flush_;
            case kEndptStat:      return endpt_stat_;
            case kEndptComplete:  return 0;
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        if (IsPortsc(off)) {
            portsc_[(off - kPortscFirst) / 4] = value & kPortscCfg;
            if (off == kPortscFirst) ApplyFrameClock(FreescaleLowPowerMode::kRun);
            return;
        }
        if (off >= kEndptCtrl0 && off <= kEndptCtrl15) {
            endptctrl_[(off - kEndptCtrl0) / 4] = value; return;
        }
        switch (off) {
            case kUsbcmd:
                /* MCIMX31RM Table 32-23 RST: the controller "resets its internal pipelines,
                   timers, counters, state machines, and so on. to their initial value". zune_keel
                   usbfn.dll sub_30916AC 0x3091750 sets RST with the host controller running. */
                usbcmd_ = value | (usbcmd_ & kCmdRst);
                RunHeldStarts();
                frame_index_.SetFrameListSize(0u, FreescaleUsbFrameIndex::FrameListSizeCode(usbcmd_));
                SetFrameRunning();
                return;
            case kUsbsts:
                frame_index_.ClearStatus(0u, value & kFrameStatusBits);
                ArmFrameIrq();
                return;
            case kUsbintr:
                usbintr_ = value;
                ArmFrameIrq();
                return;
            case kUsbCtrl:   usb_ctrl_   = value; return;
            case kOtgMirror: otg_mirror_ = value; return;
            case kUsbmode:
                usbmode_ = value;
                SetFrameRunning();
                return;
            case kConfigFlag: configflag_ = value & 1u; return;  /* CF bit0; [31:1] SBZ (Table 32-13) */
            case kOtgsc:     otgsc_ = value & kOtgscRwMask; return;  /* R/O status, W1C ints unset */
            case kUlpiView:
                ulpiview_ = value | (ulpiview_ & (kUlpiWu | kUlpiRun));
                RunHeldStarts();
                return;
            /* MCIMX31RM §32.9.5.4: "In device mode this register is read only"; "This register
               cannot be written unless the Host Controller is in the 'Halted' state". */
            case kFrIndex:
                if ((usbmode_ & kUsbmodeCm) == kUsbmodeDevice || (usbcmd_ & kCmdRs) != 0u) return;
                frame_index_.WriteFrindex(0u, value);
                ArmFrameIrq();
                return;
            case kCtrlDsSeg:    ctrl_ds_seg_   = value; return;
            case kPeriodicBase: periodic_base_ = value; return;
            case kAsyncAddr:    async_addr_    = value; return;
            case kTxFillTune:   tx_fill_tune_  = value; return;
            case kEndptPrime:
                endpt_prime_ |= value;
                RunHeldStarts();
                return;
            case kEndptFlush:
                endpt_flush_ |= value;
                endpt_prime_ &= ~value;
                RunHeldStarts();
                return;
            case kEndptSetupStat: return;
            case kEndptComplete:  return;
            case kEndptStat:      return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("portsc", portsc_, sizeof(portsc_));
        w.Write("usbcmd", usbcmd_);
        w.Write("usbintr", usbintr_);
        w.Write("usb_ctrl", usb_ctrl_);
        w.Write("otg_mirror", otg_mirror_);
        w.Write("usbmode", usbmode_);
        w.Write("configflag", configflag_);
        w.Write("otgsc", otgsc_);
        w.Write("ulpiview", ulpiview_);
        frame_index_.Save(w);
        w.Write("ctrl_ds_seg", ctrl_ds_seg_);
        w.Write("periodic_base", periodic_base_);
        w.Write("async_addr", async_addr_);
        w.Write("tx_fill_tune", tx_fill_tune_);
        w.Write("endpt_stat", endpt_stat_);
        w.Write("endpt_prime", endpt_prime_);
        w.Write("endpt_flush", endpt_flush_);
        w.WriteBytes("endptctrl", endptctrl_, sizeof(endptctrl_));
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes("portsc", portsc_, sizeof(portsc_));
        r.Read("usbcmd", usbcmd_);
        r.Read("usbintr", usbintr_);
        r.Read("usb_ctrl", usb_ctrl_);
        r.Read("otg_mirror", otg_mirror_);
        r.Read("usbmode", usbmode_);
        r.Read("configflag", configflag_);
        r.Read("otgsc", otgsc_);
        r.Read("ulpiview", ulpiview_);
        frame_index_.Restore(r);
        r.Read("ctrl_ds_seg", ctrl_ds_seg_);
        r.Read("periodic_base", periodic_base_);
        r.Read("async_addr", async_addr_);
        r.Read("tx_fill_tune", tx_fill_tune_);
        r.Read("endpt_stat", endpt_stat_);
        r.Read("endpt_prime", endpt_prime_);
        r.Read("endpt_flush", endpt_flush_);
        r.ReadBytes("endptctrl", endptctrl_, sizeof(endptctrl_));
        clock_->Disarm(irq_event_);
    }
    void PostRestore() override { ApplyFrameClock(FreescaleLowPowerMode::kRun); }

private:
    static bool IsPortsc(uint32_t off) {
        return off >= kPortscFirst && off <= kPortscLast && (off & 3u) == 0u;
    }
    /* MCIMX31RM §3.4.4.3.1: usb_main_clk is the USB DPLL output; §3.4.4.3.2: "ipg_clk_usb_baud is
       the clock for the USBOTG module". */
    bool UpllRuns() const { return plls_->Running(1u); }
    bool CoreClocked() const { return module_clocks_->Running(FreescaleModule::kUsbotg) && UpllRuns(); }

    void OnUsbClockChange() {
        RunHeldStarts();
        SetFrameRunning();
    }

    void RunHeldStarts() {
        if (!CoreClocked()) return;
        if (usbcmd_ & kCmdRst) {
            frame_index_.Reset(0u);
            usbcmd_ &= ~kCmdRst;
        }
        ulpiview_    &= ~(kUlpiWu | kUlpiRun);
        endpt_stat_   = (endpt_stat_ & ~endpt_flush_) | endpt_prime_;
        endpt_prime_  = 0u;
        endpt_flush_  = 0u;
    }

    void ResetController() {
        for (auto& p : portsc_) p = 0u;
        for (auto& e : endptctrl_) e = 0u;
        usbcmd_        = kUsbcmdReset;
        usbintr_       = 0u;
        usb_ctrl_      = 0u;
        otg_mirror_    = 0u;
        usbmode_       = 0u;
        configflag_    = 1u;
        otgsc_         = 0u;
        ulpiview_      = 0u;
        ctrl_ds_seg_   = 0u;
        periodic_base_ = 0u;
        async_addr_    = 0u;
        tx_fill_tune_  = 0u;
        endpt_stat_    = 0u;
        endpt_prime_   = 0u;
        endpt_flush_   = 0u;
        frame_index_.ResetAll();
        clock_->Disarm(irq_event_);
        avic_->DeassertSource(kAvicSourceUsbOtg);
    }

    /* MCIMX31RM §32.9.5.4: the host controller's FRINDEX "updates every 125 microseconds";
       Table 32-24 SRI: a device "initialized to FS before connect" sets it "at an interval of
       1ms during the prelude to connect and chirp"; Table 32-23 RS: a device "attach event". */
    void SetFrameRunning() {
        const uint32_t cm   = usbmode_ & kUsbmodeCm;
        const bool     host = cm == kUsbmodeHost;
        const bool     rs   = (usbcmd_ & kCmdRs) != 0u;
        frame_index_.SetRunning(0u, rs && (host || cm == kUsbmodeDevice),
                                host ? FreescaleUsbFrameIndex::Role::kHost
                                     : FreescaleUsbFrameIndex::Role::kDevicePrelude);
        ApplyFrameClock(FreescaleLowPowerMode::kRun);
    }

    void ApplyFrameClock(FreescaleLowPowerMode mode) {
        frame_index_.SetClocked(0u, TransceiverClockRuns(mode));
        ArmFrameIrq();
    }

    /* MCIMX31RM §32.12.3.1: micro-frames are "timed precisely to 125 us using the transceiver
       clock"; §32.3.7: the MUX defaults to Serial, PTS 0b10 selects ULPI; Table 32-32 PHCD:
       "Writing this bit to a 1b will disable the PHY clock". ULPI 1.1 §3.2: the PHY provides it. */
    bool TransceiverClockRuns(FreescaleLowPowerMode mode) const {
        const uint32_t portsc = portsc_[0];
        if ((portsc >> kPortscPtsShift) == kPtsUlpi) return (portsc & kPortscPhcd) == 0u;
        return module_clocks_->ModuleRunsIn(FreescaleModule::kUsbotg, mode) && UpllRuns();
    }

    /* MCIMX31RM Table 32-25: SRE / FRE make the controller "issue an interrupt" while SRI /
       FRI are set. */
    void ArmFrameIrq() {
        const uint32_t enabled = usbintr_ & kFrameStatusBits;
        if ((frame_index_.Status(0u) & enabled) != 0u) {
            avic_->AssertSource(kAvicSourceUsbOtg);
            clock_->Disarm(irq_event_);
            return;
        }
        avic_->DeassertSource(kAvicSourceUsbOtg);
        const uint64_t at = enabled != 0u ? frame_index_.NextStatusCycle(0u, enabled)
                                          : FreescaleUsbFrameIndex::kNever;
        if (at == FreescaleUsbFrameIndex::kNever) clock_->Disarm(irq_event_);
        else                                      clock_->Arm(irq_event_, at);
    }

    FreescaleModuleClocks* module_clocks_ = nullptr;
    FreescaleTimerClocks*  timer_clocks_  = nullptr;
    Imx31Plls*             plls_          = nullptr;
    GuestCycleClock*       clock_         = nullptr;
    Imx31Avic*             avic_          = nullptr;
    GuestCycleClock::Event* irq_event_    = nullptr;
    FreescaleUsbFrameIndex frame_index_{emu_};
    uint32_t portsc_[8] = {};
    uint32_t usbcmd_     = kUsbcmdReset;
    uint32_t usbintr_    = 0;
    uint32_t usb_ctrl_   = 0;
    uint32_t otg_mirror_ = 0;
    uint32_t usbmode_    = 0;
    uint32_t configflag_ = 1;  /* reset value 1 (Table 32-13) */
    uint32_t otgsc_      = 0;  /* R/W interrupt-enable + control bits */
    uint32_t ulpiview_   = 0;
    uint32_t ctrl_ds_seg_   = 0;
    uint32_t periodic_base_ = 0;
    uint32_t async_addr_    = 0;
    uint32_t tx_fill_tune_  = 0;
    uint32_t endpt_stat_    = 0;
    uint32_t endpt_prime_   = 0;
    uint32_t endpt_flush_   = 0;
    uint32_t endptctrl_[16] = {};  /* ENDPTCTRL0..15 (§32.9.5.18) */
};

}  /* namespace */

REGISTER_SERVICE(Imx31Usbotg);

}
