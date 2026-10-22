#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apb_peripheral.h"
#include "imx28_digctl.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_usbctrl.h"

#include <cstdint>
#include <mutex>

namespace cerfcore {

namespace {

/* Sections 31.3.7-31.3.10 GPTIMERnLD [23:0], GPTIMERnCTRL GPTRUN [31], GPTRST [30], GPTMODE
   [24], GPTCNT [23:0]. */
constexpr uint32_t kGptLdMask = 0x00FFFFFFu;
constexpr uint32_t kGptRun    = 1u << 31;
constexpr uint32_t kGptRst    = 1u << 30;
constexpr uint32_t kGptMode   = 1u << 24;

/* Sections 31.3.11 SBUSCFG [2:0], 31.3.22 DEVICEADDR [31:24], 31.3.21 PERIODICLISTBASE [31:12],
   31.3.24 ENDPOINTLISTADDR [31:11], 31.3.23 ASYNCLISTADDR [31:5], 31.3.25 TTCTRL [30:24], 31.3.26
   BURSTSIZE [15:0] = 0x1010, 31.3.28 IC_USB [3:0], 31.3.20 FRINDEX [13:0], 31.3.15, 31.3.31 [23:16],[7:0]. */
constexpr uint32_t kSbusCfgMask    = 0x7u;
constexpr uint32_t kAddrMaskDevice = 0xFF000000u;
constexpr uint32_t kAddrMaskHost   = 0xFFFFF000u;
constexpr uint32_t kListMaskDevice = 0xFFFFF800u;
constexpr uint32_t kListMaskHost   = 0xFFFFFFE0u;
constexpr uint32_t kTtCtrlMask     = 0x7F000000u;
constexpr uint32_t kBurstMask      = 0x0000FFFFu;
constexpr uint32_t kBurstReset     = 0x00001010u;
constexpr uint32_t kIcUsbMask      = 0x0000000Fu;
constexpr uint32_t kFrIndexMask    = 0x00003FFFu;
constexpr uint32_t kDciMask        = 0x0000FFFFu;
constexpr uint32_t kDciReset       = 0x00000001u;
constexpr uint32_t kEndptBits      = 0x00FF00FFu;

constexpr int64_t kNsPerMs = 1000000;

/* Section 31.3.32 PORTSC1: PR [8], SUSP [7], FPR [6]. */
constexpr uint32_t kPortPr   = 1u << 8;
constexpr uint32_t kPortSusp = 1u << 7;
constexpr uint32_t kPortFpr  = 1u << 6;

/* Section 31.3.32 PORTSC1: PSPD [27:26] 2 high speed, PFSC [24], HSP [9], CCS [0]. */
constexpr uint32_t kPortPspdHigh = 2u << 26;
constexpr uint32_t kPortPfsc     = 1u << 24;
constexpr uint32_t kPortHsp      = 1u << 9;
constexpr uint32_t kPortCcs      = 1u << 0;

/* Section 31.3.18 USBSTS: URI [6], PCI [2], UI [0]. */
constexpr uint32_t kStsUri = 1u << 6;
constexpr uint32_t kStsPci = 1u << 2;
constexpr uint32_t kStsUi  = 1u << 0;

/* Sections 31.3.22 DEVICEADDR USBADR [31:25], USBADRA [24]; 31.3.34 USBMODE SLOM [3]. */
constexpr uint32_t kAddrShift = 25u;
constexpr uint32_t kAddrAdra  = 1u << 24;
constexpr uint32_t kModeSlom  = 1u << 3;

/* Section 31.3.39 ENDPTCOMPLETE and 31.3.35 ENDPTSETUPSTAT: EP0 RX bit 0, EP0 TX bit 16. */
constexpr uint32_t kEp0Rx   = 1u << 0;
constexpr uint32_t kEp0Tx   = 1u << 16;
constexpr uint32_t kEp0Bits = kEp0Rx | kEp0Tx;

/* Sections 31.3.40-31.3.47 ENDPTCTRLn TXE [23], RXE [7]. */
constexpr uint32_t kEpTxe = 1u << 23;
constexpr uint32_t kEpRxe = 1u << 7;
constexpr uint32_t kEpTxs = 1u << 16;
constexpr uint32_t kEpRxs = 1u << 0;

/* Table 5-1 sources 93 usb0_irq and 92 usb1_irq. */
constexpr uint32_t kIrqSource[2] = {93u, 92u};

}  /* namespace */

bool Imx28Usbctrl::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Usbctrl::OnReady() {
    icoll_  = &emu_.Get<Imx28Icoll>();
    digctl_ = &emu_.Get<Imx28Digctl>();
    power_  = &emu_.Get<Imx28Power>();
    Reset();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        Reset();
        DriveLine(false);
    });
    power_->RegisterSessionListener(unit_, [this] { OnSessionChange(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Usbctrl::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 %08X: %s at %08X (value %08X, USBCMD %08X USBMODE %08X)", base_,
                          what, addr, value, r_[kCmd], r_[kMode]);
}

void Imx28Usbctrl::Reset() {
    std::lock_guard<std::mutex> guard(mtx_);
    ResetLocked();
}

/* Section 31.3 memory map, Reset value column. */
void Imx28Usbctrl::ResetLocked() {
    for (uint32_t& v : r_) v = 0u;
    for (uint32_t i = 0; i < 2u; ++i) gptld_[i] = gptctrl_[i] = gptcnt_[i] = 0u;
    for (uint32_t& e : endptctrl_) e = 0u;
    endptctrl_[0]  = kUsbEp0Reset;
    r_[kCmd]       = kUsbCmdReset;
    r_[kBurst]     = kBurstReset;
    r_[kOtgsc]     = kUsbOtgscReset;
    r_[kDciVersion] = kDciReset;
    port_          = 0u;
    mode_locked_   = false;
    onems_ack_     = emu_.Get<GuestCycleClock>().NowNs() / kNsPerMs;
    session_       = power_->Session(unit_);
}

/* Section 31.3.18: HCH 1 in host mode while RS is 0; NAKI is set while an enabled NAK bit is
   set. */
uint32_t Imx28Usbctrl::ComposeSts() const {
    uint32_t s = r_[kSts];
    if (Mode() == kUsbModeHost && (r_[kCmd] & kUsbCmdRs) == 0u) s |= kUsbStsHch;
    if ((r_[kNak] & r_[kNakEn]) != 0u) s |= kUsbStsNaki;
    return s;
}

/* Section 31.3.33: BSE, BSV, ASV and AVV read the POWER session comparators; ONEMST toggles
   and ONEMSS is set every millisecond. */
uint32_t Imx28Usbctrl::ComposeOtgsc() {
    uint32_t v = r_[kOtgsc];
    if (session_.sessend)   v |= kUsbOtgscBse;
    if (session_.bvalid)    v |= kUsbOtgscBsv;
    if (session_.avalid)    v |= kUsbOtgscAsv;
    if (session_.vbusvalid) v |= kUsbOtgscAvv;
    const int64_t ms = emu_.Get<GuestCycleClock>().NowNs() / kNsPerMs;
    if ((ms & 1) != 0) v |= kUsbOtgscOnemst;
    if (ms != onems_ack_) v |= kUsbOtgscOnemsIs;
    return v;
}

/* Section 31.3.32 default values by mode: device PE 1 and PP 1, WKDS and WKCN 0, line state
   undefined; section 32.2.4.5: both lines high with no cable attached. */
uint32_t Imx28Usbctrl::ComposePortsc() const {
    uint32_t v = kUsbPortReset | r_[kPortsc] | kUsbPortLsUndef | port_;
    if (Mode() == kUsbModeDevice) {
        v |= kUsbPortPe | kUsbPortHostPp;
        v &= ~kUsbPortWakeHost;
    }
    return v;
}

uint32_t Imx28Usbctrl::ReadReg(uint32_t off, uint32_t addr) {
    for (const Imx28UsbConst& c : kImx28UsbConsts) {
        if (c.offset == off) return c.value;
    }
    switch (off) {
        case kUsbGptLd0:         return gptld_[0];
        case kUsbGptLd1:         return gptld_[1];
        case kUsbGptCtrl0:       return gptctrl_[0] | gptcnt_[0];
        case kUsbGptCtrl1:       return gptctrl_[1] | gptcnt_[1];
        case kUsbSbusCfg:        return r_[kSbusCfg];
        case kUsbDciVersion:     return r_[kDciVersion];
        case kUsbCmd:            return r_[kCmd];
        case kUsbSts:            return ComposeSts();
        case kUsbIntr:           return r_[kIntr];
        case kUsbFrIndex:        return r_[kFrIndex];
        case kUsbAddrBase:       return r_[kAddrBase];
        case kUsbListAddr:       return r_[kListAddr];
        case kUsbTtCtrl:         return r_[kTtCtrl];
        case kUsbBurstSize:      return r_[kBurst];
        case kUsbTxFillTuning:   return r_[kTxFill];
        case kUsbIcUsb:          return r_[kIcUsb];
        case kUsbEndptNak:       return r_[kNak];
        case kUsbEndptNakEn:     return r_[kNakEn];
        case kUsbPortsc1:        return ComposePortsc();
        case kUsbOtgsc:          return ComposeOtgsc();
        case kUsbMode:           return r_[kMode];
        case kUsbEndptSetupStat: return r_[kSetupStat];
        case kUsbEndptPrime:     return 0u;
        case kUsbEndptStat:      return r_[kEndptStat];
        case kUsbEndptFlush:     return 0u;
        case kUsbEndptComplete:  return r_[kComplete];
        default:                 break;
    }
    if (off >= kUsbEndptCtrl0 && off < kUsbMapEnd) return endptctrl_[(off - kUsbEndptCtrl0) / 4u];
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

/* Section 19.4.1 HW_DIGCTL_CTRL USB0_CLKGATE: USBSTS_URI "should be not be polled" while gated;
   sharp_brain_pw_sh4 usbfn.dll c069f998 reads USBINTR while gated. */
uint32_t Imx28Usbctrl::ReadWord(uint32_t addr) {
    const uint32_t off = addr - base_;
    if (off >= kUsbMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    std::lock_guard<std::mutex> guard(mtx_);
    return ReadReg(off, addr);
}

/* Section 31.3.8: GPTRUN starts a countdown that interrupts at zero; GPTRST loads GPTCNT. */
void Imx28Usbctrl::WriteGptCtrl(uint32_t i, uint32_t value, uint32_t addr) {
    if ((value & kGptRun) != 0u) Die("GPTIMER run", addr, value);
    if ((value & kGptRst) != 0u) gptcnt_[i] = gptld_[i];
    gptctrl_[i] = value & kGptMode;
}

/* Section 31.3.17: RST resets the controller and reads back 0 once complete; RS in host mode
   runs the host schedules. */
void Imx28Usbctrl::WriteCmd(uint32_t value, uint32_t addr) {
    if ((value & kUsbCmdRst) != 0u) {
        ResetLocked();
        return;
    }
    if (Mode() == kUsbModeHost && (value & (kUsbCmdRs | kUsbCmdIaa)) != 0u) {
        Die("host controller run", addr, value);
    }
    r_[kCmd] = value & kUsbCmdWritable;
    if ((value & kUsbCmdRs) == 0u) port_ = 0u;
}

/* Section 31.3.34 CM: "this register can only be written once after reset"; section 31.3.17
   ASPE and ASP default to 1 and 3 when CM becomes host. */
void Imx28Usbctrl::WriteMode(uint32_t value, uint32_t addr) {
    const uint32_t cm   = value & kUsbModeCmMask;
    uint32_t       next = value & kUsbModeWritable & ~kUsbModeCmMask;
    if (!mode_locked_) {
        if (cm == 1u) Die("USBMODE reserved CM", addr, value);
        mode_locked_ = true;
        next |= cm;
        if (cm == kUsbModeHost) r_[kCmd] |= kUsbCmdHostAspe | kUsbCmdHostAsp;
    } else {
        next |= Mode();
    }
    r_[kMode] = next;
}

/* Section 31.3.32: PTC selects test modes, FPR forces resume, PR and SUSP are host controls. */
void Imx28Usbctrl::WritePortsc(uint32_t value, uint32_t addr) {
    const bool host = Mode() == kUsbModeHost;
    if ((value & kUsbPortPtc) != 0u || (value & kPortFpr) != 0u ||
        (host && (value & (kPortPr | kPortSusp)) != 0u)) {
        Die("PORTSC1 control", addr, value);
    }
    r_[kPortsc] = value & (kUsbPortWritable | (host ? kUsbPortHostPp : 0u));
}

/* Section 31.3.33: enables [30:24] and controls [7:0] store; statuses [22:16] clear on 1. */
void Imx28Usbctrl::WriteOtgsc(uint32_t value, uint32_t addr) {
    if ((value & kUsbOtgscOnemse) != 0u) Die("OTGSC ONEMSE", addr, value);
    const uint32_t keep = kUsbOtgscWritable & ~kUsbOtgscW1c;
    if ((value & kUsbOtgscOnemsIs) != 0u) {
        onems_ack_ = emu_.Get<GuestCycleClock>().NowNs() / kNsPerMs;
    }
    const uint32_t status = r_[kOtgsc] & kUsbOtgscW1c & ~value;
    r_[kOtgsc] = (r_[kOtgsc] & ~kUsbOtgscWritable) | (value & keep) | status;
}

void Imx28Usbctrl::WriteReg(uint32_t off, uint32_t value, uint32_t addr) {
    const bool host = Mode() == kUsbModeHost;
    switch (off) {
        case kUsbGptLd0:     gptld_[0] = value & kGptLdMask; return;
        case kUsbGptLd1:     gptld_[1] = value & kGptLdMask; return;
        case kUsbGptCtrl0:   WriteGptCtrl(0u, value, addr); return;
        case kUsbGptCtrl1:   WriteGptCtrl(1u, value, addr); return;
        case kUsbSbusCfg:    r_[kSbusCfg] = value & kSbusCfgMask; return;
        case kUsbDciVersion: r_[kDciVersion] = value & kDciMask; return;
        case kUsbCmd:        WriteCmd(value, addr); return;
        case kUsbSts:        r_[kSts] &= ~(value & kUsbStsW1c); return;
        case kUsbIntr:       r_[kIntr] = value & kUsbIntrWritable; return;
        case kUsbFrIndex:    r_[kFrIndex] = value & kFrIndexMask; return;
        case kUsbAddrBase:
            if (!host && (value & kAddrAdra) != 0u) Die("DEVICEADDR USBADRA", addr, value);
            r_[kAddrBase] = value & (host ? kAddrMaskHost : kAddrMaskDevice);
            return;
        case kUsbListAddr:   r_[kListAddr] = value & (host ? kListMaskHost : kListMaskDevice); return;
        case kUsbTtCtrl:     r_[kTtCtrl] = value & kTtCtrlMask; return;
        case kUsbBurstSize:  r_[kBurst] = value & kBurstMask; return;
        case kUsbTxFillTuning: r_[kTxFill] = value & kUsbTxFillMask; return;
        case kUsbIcUsb:      r_[kIcUsb] = value & kIcUsbMask; return;
        case kUsbEndptNak:   r_[kNak] &= ~value; return;
        case kUsbEndptNakEn: r_[kNakEn] = value & kEndptBits; return;
        case kUsbPortsc1:    WritePortsc(value, addr); return;
        case kUsbOtgsc:      WriteOtgsc(value, addr); return;
        case kUsbMode:       WriteMode(value, addr); return;
        case kUsbEndptSetupStat: r_[kSetupStat] &= ~value; return;
        case kUsbEndptPrime:
            if (host && (value & kEndptBits) != 0u) Die("ENDPTPRIME in host mode", addr, value);
            Prime(value & kEndptBits, addr, value);
            return;
        case kUsbEndptFlush: r_[kEndptStat] &= ~(value & kEndptBits); return;
        case kUsbEndptComplete: r_[kComplete] &= ~value; return;
        default: break;
    }
    if (off >= kUsbEndptCtrl0 && off < kUsbMapEnd) {
        const uint32_t ep = (off - kUsbEndptCtrl0) / 4u;
        uint32_t ep0 = kUsbEp0Reset | (value & kUsbEp0Writable);
        if ((r_[kSetupStat] & kEp0Rx) != 0u) ep0 &= ~(kEpTxs | kEpRxs);
        endptctrl_[ep] = ep == 0u ? ep0 : (value & kUsbEpWritable);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx28Usbctrl::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - base_;
    if (off >= kUsbMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    if (digctl_->UsbClockGated(unit_)) Die("WriteWord with USBn_CLKGATE set", addr, value);
    for (const Imx28UsbConst& c : kImx28UsbConsts) {
        if (c.offset == off) HaltUnsupportedAccess("WriteWord", addr, value);
    }
    {
        std::lock_guard<std::mutex> guard(mtx_);
        WriteReg(off, value, addr);
    }
    DriveLine(false);
    NotifyLink();
}

/* Sections 31.3.36 and 31.3.38: priming fetches the dTD, clears the prime bit and sets the
   ENDPTSTAT ready bit. */
void Imx28Usbctrl::Prime(uint32_t bits, uint32_t addr, uint32_t value) {
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        if (((bits >> bit) & 1u) == 0u) continue;
        const uint32_t ep = bit & 0x7u;
        const bool     tx = bit >= 16u;
        if (ep != 0u && (endptctrl_[ep] & (tx ? kEpTxe : kEpRxe)) == 0u) {
            Die("ENDPTPRIME of a disabled endpoint", addr, value);
        }
        endpoints_.Fetch(r_[kListAddr], bit, value);
        r_[kEndptStat] |= 1u << bit;
    }
    if (bits != 0u && on_prime_) on_prime_();
}

void Imx28Usbctrl::AttachHost(std::function<void()> on_link, std::function<void()> on_prime) {
    on_link_  = std::move(on_link);
    on_prime_ = std::move(on_prime);
}

void Imx28Usbctrl::NotifyLink() {
    if (on_link_) on_link_();
}

/* Section 31.2.6.6.2 Figure 31-33: Run/Stop moves the device to the attach state; sharp_brain_pw_sh4
   usbfn.dll c06a1818 treats OTGSC BSV clear as detach. */
bool Imx28Usbctrl::Linked() {
    std::lock_guard<std::mutex> guard(mtx_);
    return Mode() == kUsbModeDevice && (r_[kCmd] & kUsbCmdRs) != 0u && session_.bvalid;
}

void Imx28Usbctrl::Raise(uint32_t sts) {
    {
        std::lock_guard<std::mutex> guard(mtx_);
        r_[kSts] |= sts;
    }
    DriveLine(false);
}

/* Section 31.2.6.6.2.1: a bus reset sets the address to 0, disables endpoints 1-7, cancels primed
   transfers and sets URI; section 31.3.32 PR is the device-mode reset status. */
void Imx28Usbctrl::BusReset() {
    {
        std::lock_guard<std::mutex> guard(mtx_);
        r_[kAddrBase] = 0u;
        for (uint32_t ep = 1u; ep < kEndpoints; ++ep) endptctrl_[ep] &= ~(kEpTxe | kEpRxe);
        r_[kEndptStat] = 0u;
        port_ |= kPortPr;
    }
    Raise(kStsUri);
}

/* Section 31.3.18 PCI: set when the port enters the full or high-speed state; section 31.3.32
   PFSC disables the high-speed chirp, PSPD and HSP give the speed, CCS the attach. */
void Imx28Usbctrl::BusResetEnd() {
    {
        std::lock_guard<std::mutex> guard(mtx_);
        const uint32_t speed = (r_[kPortsc] & kPortPfsc) != 0u ? 0u : (kPortPspdHigh | kPortHsp);
        port_ = (port_ & ~kPortPr) | kPortCcs | speed;
    }
    Raise(kStsPci);
}

/* Sections 31.2.6.6.3.2.1-31.2.6.6.3.2.2, 31.3.40 ENDPTCTRL0 TXS/RXS and Table 31-54 IOS: a SETUP
   fills the setup buffer, sets ENDPTSETUPSTAT, clears the EP0 stall and prime status. */
void Imx28Usbctrl::Setup(const uint8_t* setup) {
    {
        std::lock_guard<std::mutex> guard(mtx_);
        if (Mode() != kUsbModeDevice) Die("SETUP outside device mode", base_ + kUsbMode, r_[kMode]);
        if ((r_[kSetupStat] & kEp0Rx) != 0u && (r_[kMode] & kModeSlom) == 0u) {
            Die("SETUP with setup lockout engaged", base_ + kUsbEndptSetupStat, r_[kSetupStat]);
        }
        endpoints_.WriteSetup(r_[kListAddr], setup);
        r_[kSetupStat] |= kEp0Rx;
        r_[kEndptStat] &= ~kEp0Bits;
        endptctrl_[0] &= ~(kEpTxs | kEpRxs);
        if (endpoints_.InterruptOnSetup(r_[kListAddr])) r_[kSts] |= kStsUi;
    }
    DriveLine(false);
}

/* Table 31-67: a primed endpoint transfers, an unprimed one NAKs, a stalled one STALLs; sections
   31.3.39 ENDPTCOMPLETE and 31.3.18 UI (IOC or short packet received). */
bool Imx28Usbctrl::Transact(uint32_t bit, uint8_t* data, uint32_t length,
                            Imx28UsbEndpoints::Transfer& out) {
    {
        std::lock_guard<std::mutex> guard(mtx_);
        const uint32_t ep = bit & 0x7u;
        const bool     tx = bit >= 16u;
        if (Mode() != kUsbModeDevice) Die("transfer outside device mode", base_ + kUsbMode, r_[kMode]);
        if ((endptctrl_[ep] & (tx ? kEpTxs : kEpRxs)) != 0u) {
            Die("host transfer to a stalled endpoint", base_ + kUsbEndptCtrl0 + ep * 4u, endptctrl_[ep]);
        }
        if ((r_[kEndptStat] & (1u << bit)) == 0u) return false;
        out = endpoints_.Execute(r_[kListAddr], bit, data, length);
        if (!out.more) r_[kEndptStat] &= ~(1u << bit);
        r_[kComplete] |= 1u << bit;
        if (out.ioc || (!tx && out.short_packet)) r_[kSts] |= kStsUi;
    }
    DriveLine(false);
    return true;
}

uint32_t Imx28Usbctrl::DeviceAddress() {
    std::lock_guard<std::mutex> guard(mtx_);
    return r_[kAddrBase] >> kAddrShift;
}

/* Section 31.3.33 BSEIS, BSVIS, ASVIS and AVVIS: a session comparator changed. */
void Imx28Usbctrl::OnSessionChange() {
    {
        std::lock_guard<std::mutex> guard(mtx_);
        const Imx28Power::UsbSession now = power_->Session(unit_);
        if (now.sessend != session_.sessend)     r_[kOtgsc] |= kUsbOtgscBseIs;
        if (now.bvalid != session_.bvalid)       r_[kOtgsc] |= kUsbOtgscBsvIs;
        if (now.avalid != session_.avalid)       r_[kOtgsc] |= kUsbOtgscAsvIs;
        if (now.vbusvalid != session_.vbusvalid) r_[kOtgsc] |= kUsbOtgscAvvIs;
        session_ = now;
    }
    DriveLine(false);
    NotifyLink();
}

/* Sections 31.3.18-31.3.19 and 31.3.33: a status bit with its enable raises the unit interrupt. */
void Imx28Usbctrl::DriveLine(bool force) {
    std::lock_guard<std::mutex> guard(mtx_);
    const uint32_t otg = r_[kOtgsc];
    const bool on = (ComposeSts() & r_[kIntr]) != 0u ||
                    ((otg & kUsbOtgscW1c & ((otg & kUsbOtgscEnables) >> 8)) != 0u);
    if (!force && on == line_) return;
    line_ = on;
    if (on) icoll_->AssertSource(kIrqSource[unit_]);
    else    icoll_->DeassertSource(kIrqSource[unit_]);
}

void Imx28Usbctrl::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> guard(mtx_);
    w.Write("gptld", gptld_);
    w.Write("gptctrl", gptctrl_);
    w.Write("gptcnt", gptcnt_);
    w.Write("endptctrl", endptctrl_);
    w.Write("regs", r_);
    w.Write("onems_ack", onems_ack_);
    w.Write("port", port_);
    w.Write<uint8_t>("mode_locked", mode_locked_ ? 1u : 0u);
}

void Imx28Usbctrl::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> guard(mtx_);
    uint8_t locked = 0u;
    r.Read("gptld", gptld_);
    r.Read("gptctrl", gptctrl_);
    r.Read("gptcnt", gptcnt_);
    r.Read("endptctrl", endptctrl_);
    r.Read("regs", r_);
    r.Read("onems_ack", onems_ack_);
    r.Read("port", port_);
    r.Read("mode_locked", locked);
    mode_locked_ = locked != 0u;
    session_ = power_->Session(unit_);
}

}
