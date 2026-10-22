#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apb_peripheral.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_lradc_adc.h"
#include "imx28_lradc_delay.h"
#include "imx28_lradc_inputs.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <memory>

namespace cerfcore {

namespace {

/* Section 38.5 memory map. */
constexpr uint32_t kCtrl0       = 0x000u;
constexpr uint32_t kCtrl1       = 0x010u;
constexpr uint32_t kCtrl2       = 0x020u;
constexpr uint32_t kCtrl3       = 0x030u;
constexpr uint32_t kStatus      = 0x040u;
constexpr uint32_t kCh0         = 0x050u;
constexpr uint32_t kChEnd       = 0x0D0u;
constexpr uint32_t kDelay0      = 0x0D0u;
constexpr uint32_t kDelayEnd    = 0x110u;
constexpr uint32_t kDebug0      = 0x110u;
constexpr uint32_t kDebug1      = 0x120u;
constexpr uint32_t kConversion  = 0x130u;
constexpr uint32_t kCtrl4       = 0x140u;
constexpr uint32_t kThreshold0  = 0x150u;
constexpr uint32_t kThreshold1  = 0x160u;
constexpr uint32_t kVersion     = 0x170u;
constexpr uint32_t kMapEnd      = 0x180u;
constexpr uint32_t kRegStride   = 0x010u;

constexpr uint32_t kChannels = 8u;
constexpr uint32_t kDelays   = 4u;

/* Section 38.5.1 CTRL0 reset C000_0000h: ONCHIP_GROUNDREF [26], BUTTON1_DETECT_ENABLE [25],
   BUTTON0_DETECT_ENABLE [24], TOUCH_DETECT_ENABLE [23], SCHEDULE [7:0]. */
constexpr uint32_t kCtrl0Reset    = 0xC0000000u;
constexpr uint32_t kCtrl0Writable = 0xC7FF00FFu;
constexpr uint32_t kGroundRef     = 1u << 26;
constexpr uint32_t kButton1Detect = 1u << 25;
constexpr uint32_t kButton0Detect = 1u << 24;
constexpr uint32_t kTouchDetect   = 1u << 23;
constexpr uint32_t kScheduleMask  = 0xFFu;

/* Section 38.5.2 CTRL1: IRQ_EN [28:16], IRQ [12:0]: LRADCn [n], TOUCH [8], THRESHOLD0 [9],
   THRESHOLD1 [10], BUTTON0 [11], BUTTON1 [12]. */
constexpr uint32_t kCtrl1Writable = 0x1FFF1FFFu;
constexpr uint32_t kIrqMask       = 0x1FFFu;
constexpr uint32_t kIrqEnShift    = 16u;
constexpr uint32_t kIrqTouch      = 1u << 8;
constexpr uint32_t kIrqButton0    = 1u << 11;
constexpr uint32_t kIrqButton1    = 1u << 12;
constexpr uint32_t kIrqBits       = 13u;

/* Table 5-1 sources 16-23 lradc_ch0-7_irq, 10 lradc_touch_irq, 14-15 lradc_thresh0-1_irq,
   24-25 lradc_button0-1_irq, in CTRL1 bit order. */
constexpr uint32_t kIrqSource[kIrqBits] = {16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u,
                                           10u, 14u, 15u, 24u, 25u};

/* Section 38.5.3 CTRL2 reset 0000_8000h: VTHSENSE [14:13], DISABLE_MUXAMP_BYPASS [12]. */
constexpr uint32_t kCtrl2Reset    = 0x00008000u;
constexpr uint32_t kCtrl2Writable = 0xFF00F3FFu;
constexpr uint32_t kCtrl2Frozen   = 0x00007000u;

/* Section 38.5.4 CTRL3: FORCE_ANALOG_PWUP [23], FORCE_ANALOG_PWDN [22], CYCLE_TIME [9:8]. */
constexpr uint32_t kCtrl3Writable  = 0x03C00333u;
constexpr uint32_t kCtrl3Cycle     = 0x00000300u;
constexpr uint32_t kCtrl3PowerUp   = 1u << 23;
constexpr uint32_t kCtrl3PowerDown = 1u << 22;

/* Section 38.5.21 CTRL4 reset 7654_3210h. */
constexpr uint32_t kCtrl4Reset    = 0x76543210u;

/* Section 38.5.5 STATUS reset 1FFF_0000h: *_PRESENT [28:16]; BUTTON1_DETECT_RAW [2],
   BUTTON0_DETECT_RAW [1], TOUCH_DETECT_RAW [0]. */
constexpr uint32_t kStatusPresent = 0x1FFF0000u;

/* Section 38.5.6 CHn: TOGGLE [31], ACCUMULATE [29], NUM_SAMPLES [28:24], VALUE [17:0];
   section 38.5.13 CH7 TESTMODE_TOGGLE [30]. */
constexpr uint32_t kChWritable   = 0x3F03FFFFu;
constexpr uint32_t kChValueMask  = 0x3FFFFu;
constexpr uint32_t kChToggle     = 1u << 31;
constexpr uint32_t kChAccumulate = 1u << 29;
constexpr uint32_t kChNumShift   = 24u;
constexpr uint32_t kChNumMask    = 0x1Fu;

/* Section 38.5.14 DELAYn: TRIGGER_LRADCS [31:24], KICK [20], TRIGGER_DELAYS [19:16],
   LOOP_COUNT [15:11], DELAY [10:0]. */
constexpr uint32_t kDelayWritable = 0xFF1FFFFFu;
constexpr uint32_t kDelayKick     = 1u << 20;
constexpr uint32_t kDelayLoopShift = 11u;
constexpr uint32_t kDelayLoopMask  = 0x1Fu;
constexpr uint32_t kDelayCount     = 0x7FFu;

/* Section 38.5.20 CONVERSION reset 0000_0080h: AUTOMATIC [20], SCALE_FACTOR [17:16],
   SCALED_BATT_VOLTAGE [9:0]; 0x2 LI_ION "29/128". */
constexpr uint32_t kConversionReset    = 0x00000080u;
constexpr uint32_t kConversionWritable = 0x001303FFu;
constexpr uint32_t kConvAutomatic      = 1u << 20;
constexpr uint32_t kConvScaledMask     = 0x3FFu;
constexpr uint32_t kConvLiIonNumerator   = 29u;
constexpr uint32_t kConvLiIonDenominator = 128u;

/* Table 38-1 mux select 7 is BATT. */
constexpr uint32_t kBattSelect = 7u;

/* Sections 38.5.22, 38.5.23 THRESHOLDn: ENABLE [24]; section 38.5.24 VERSION 0103_0000h. */
constexpr uint32_t kThresholdWritable = 0x01FFFFFFu;
constexpr uint32_t kThresholdEnable   = 1u << 24;
constexpr uint32_t kVersionValue      = 0x01030000u;

/* Section 39.3: _SET, _CLR and _TOG exist for the registers that are not read-only. */
bool HasSctAlias(uint32_t reg) {
    if (reg == kStatus || reg == kVersion || reg == kDebug0 || reg == kDebug1) return false;
    return reg <= kThreshold1;
}

struct Channel {
    uint32_t value;
    uint8_t  toggle;
    uint8_t  accumulate;
    uint8_t  samples;
};

class Imx28Lradc : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx28;
    }

    void OnReady() override {
        icoll_ = &emu_.Get<Imx28Icoll>();
        adc_   = std::make_unique<Imx28LradcAdc>(emu_);
        timer_.Init([this](uint32_t d) { OnDelayExpiry(d); });
        emu_.Get<GuestCycleClock>().RegisterRateListener([this] { timer_.Rescale(); });
        ResetBlock();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
        if (auto* inputs = emu_.TryGet<Imx28LradcInputs>()) {
            inputs->RegisterChangeListener([this] {
                UpdateDetectors();
                DriveLines(false);
            });
        }
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kImx28LradcBase; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     SaveState(StateWriter& w) override;
    void     RestoreState(StateReader& r) override;
    void     PostRestore() override {
        timer_.Rearm();
        DriveLines(true);
    }

private:
    uint32_t ChannelReg(uint32_t n) const;
    bool     DetectorLevel(uint32_t detector);
    void     UpdateDetectors();
    void     ResetBlock();
    void     DriveLines(bool force);
    void     RunSchedule();
    void     ConvertChannel(uint32_t n);
    void     KickDelay(uint32_t d);
    void     OnDelayExpiry(uint32_t d);
    void     WriteCtrl0(Imx28SctOp op, uint32_t value);
    void     WriteChannel(uint32_t n, Imx28SctOp op, uint32_t value);
    void     WriteDelay(uint32_t d, Imx28SctOp op, uint32_t value);
    void     WriteRegister(uint32_t reg, Imx28SctOp op, uint32_t addr, uint32_t value);

    std::unique_ptr<Imx28LradcAdc> adc_;
    Imx28Icoll* icoll_ = nullptr;

    uint32_t ctrl0_ = 0u;
    uint32_t ctrl1_ = 0u;
    uint32_t ctrl2_ = 0u;
    uint32_t ctrl3_ = 0u;
    uint32_t ctrl4_ = 0u;
    uint32_t conversion_ = 0u;
    uint32_t threshold_[2] = {};
    uint32_t delay_[kDelays] = {};
    Channel  ch_[kChannels] = {};
    Imx28LradcDelay timer_{emu_};
    bool     detect_[3] = {};
    uint32_t line_ = 0u;
};

}  /* namespace */

void Imx28Lradc::ResetBlock() {
    ctrl0_ = kCtrl0Reset;
    ctrl1_ = 0u;
    ctrl2_ = kCtrl2Reset;
    ctrl3_ = 0u;
    ctrl4_ = kCtrl4Reset;
    conversion_ = kConversionReset;
    threshold_[0] = threshold_[1] = 0u;
    for (uint32_t& d : delay_) d = 0u;
    for (Channel& c : ch_) c = Channel{};
    timer_.Reset();
    for (bool& d : detect_) d = false;
    DriveLines(false);
}

uint32_t Imx28Lradc::ChannelReg(uint32_t n) const {
    const Channel& c = ch_[n];
    return (static_cast<uint32_t>(c.toggle) << 31) | (static_cast<uint32_t>(c.accumulate) << 29) |
           (static_cast<uint32_t>(c.samples) << kChNumShift) | c.value;
}

/* Section 38.5.5 *_DETECT_RAW: status of the detect comparator in the analog section. */
bool Imx28Lradc::DetectorLevel(uint32_t detector) {
    Imx28LradcInputs* inputs = emu_.TryGet<Imx28LradcInputs>();
    if (inputs == nullptr) {
        emu_.Get<Fatal>().Die("Imx28 %08X: detector %u level with no input model, CTRL0 %08X",
                              MmioBase(), detector, ctrl0_);
    }
    return detector == 0u ? inputs->TouchDetected(ctrl0_)
                          : inputs->ButtonDetected(detector - 1u, ctrl0_);
}

/* Section 38.5.2 TOUCH_DETECT_IRQ, BUTTONn_DETECT_IRQ: set on detection, cleared by software;
   section 38.5.1 detector enable bits [25:23]. */
void Imx28Lradc::UpdateDetectors() {
    static constexpr uint32_t kEnable[3] = {kTouchDetect, kButton0Detect, kButton1Detect};
    static constexpr uint32_t kIrq[3]    = {kIrqTouch, kIrqButton0, kIrqButton1};
    if (Gated(ctrl0_)) return;
    for (uint32_t i = 0; i < 3u; ++i) {
        const bool now = (ctrl0_ & kEnable[i]) != 0u && DetectorLevel(i);
        if (now && !detect_[i]) ctrl1_ |= kIrq[i];
        detect_[i] = now;
    }
}

/* Section 38.5.2: each interrupt is its status bit ANDed with its enable; Table 5-1 sources. */
void Imx28Lradc::DriveLines(bool force) {
    const uint32_t active = ctrl1_ & (ctrl1_ >> kIrqEnShift) & kIrqMask;
    for (uint32_t b = 0; b < kIrqBits; ++b) {
        const bool on   = ((active >> b) & 1u) != 0u;
        const bool held = ((line_ >> b) & 1u) != 0u;
        if (!force && on == held) continue;
        line_ = on ? (line_ | (1u << b)) : (line_ & ~(1u << b));
        if (on) icoll_->AssertSource(kIrqSource[b]);
        else    icoll_->DeassertSource(kIrqSource[b]);
    }
}

/* Section 38.2.3: scheduled channels convert in channel order, clear their schedule bit and
   set their interrupt status. */
void Imx28Lradc::RunSchedule() {
    for (uint32_t n = 0; n < kChannels; ++n) {
        if ((ctrl0_ & (1u << n)) == 0u) continue;
        ConvertChannel(n);
        ctrl0_ &= ~(1u << n);
    }
}

/* Section 38.5.6 ACCUMULATE: "add successive samples to the 18 bit accumulator". */
void Imx28Lradc::ConvertChannel(uint32_t n) {
    Channel& c = ch_[n];
    if (c.samples != 0u) {
        emu_.Get<Fatal>().Die("Imx28 %08X: CH%u conversion with CH %08X NUM_SAMPLES %u", MmioBase(),
                              n, ChannelReg(n), static_cast<unsigned>(c.samples));
    }
    if ((ctrl3_ & kCtrl3PowerDown) != 0u) {
        emu_.Get<Fatal>().Die("Imx28 %08X: CH%u conversion with CTRL3 %08X FORCE_ANALOG_PWDN set",
                              MmioBase(), n, ctrl3_);
    }
    const uint32_t select = (ctrl4_ >> (4u * n)) & 0xFu;
    const uint32_t sample = adc_->Convert(n, select, ctrl0_, ctrl2_, conversion_);
    /* Section 38.5.20 AUTOMATIC: "compute the scaled battery voltage each time an LRADC Channel 7
       (BATT) conversion takes place"; SCALED_BATT_VOLTAGE is battery voltage times 29/128. */
    if (select == kBattSelect && (conversion_ & kConvAutomatic) != 0u) {
        conversion_ = (conversion_ & ~kConvScaledMask) |
                      ((sample * kConvLiIonNumerator / kConvLiIonDenominator) & kConvScaledMask);
    }
    c.toggle ^= 1u;
    c.value = c.accumulate != 0u ? (c.value + sample) & kChValueMask : sample;
    ctrl1_ |= 1u << n;
}

/* Section 38.2.4: DELAY must be non-zero; LOOP_COUNT 0 is one loop. */
void Imx28Lradc::KickDelay(uint32_t d) {
    const uint32_t reg = delay_[d];
    if ((reg & kDelayCount) == 0u) return;
    timer_.Kick(d, reg & kDelayCount, ((reg >> kDelayLoopShift) & kDelayLoopMask) + 1u);
}

/* Section 38.5.14 TRIGGER_LRADCS: sets the CTRL0.SCHEDULE bits; TRIGGER_DELAYS: triggers the
   delay channels, "including the one that issues the trigger". CLKGATE gates the block clocks. */
void Imx28Lradc::OnDelayExpiry(uint32_t d) {
    if (Gated(ctrl0_)) {
        timer_.Cancel(d);
        return;
    }
    const uint32_t reg = delay_[d];
    ctrl0_ |= (reg >> 24) & kScheduleMask;
    RunSchedule();
    for (uint32_t t = 0; t < kDelays; ++t) {
        if (((reg >> 16) & (1u << t)) != 0u) KickDelay(t);
    }
    DriveLines(false);
}

void Imx28Lradc::WriteCtrl0(Imx28SctOp op, uint32_t value) {
    const uint32_t old  = ctrl0_;
    uint32_t       next = Imx28ApplySct(op, old, value);
    next = (old & ~kCtrl0Writable) | (next & kCtrl0Writable);
    if (IsSoftReset(old, next)) {
        ResetBlock();
        return;
    }
    if ((next & kGroundRef) != 0u || (Gated(next) && (next & kScheduleMask) != 0u)) {
        emu_.Get<Fatal>().Die("Imx28 %08X: CTRL0 %08X -> %08X", MmioBase(), old, next);
    }
    ctrl0_ = next;
    UpdateDetectors();
    RunSchedule();
    DriveLines(false);
}

/* Sections 38.5.6, 38.5.13 CHn: TOGGLE [31], ACCUMULATE [29], NUM_SAMPLES [28:24], VALUE [17:0] are
   R/W; CH7 TESTMODE_TOGGLE [30] has no W access. */
void Imx28Lradc::WriteChannel(uint32_t n, Imx28SctOp op, uint32_t value) {
    Channel&       c    = ch_[n];
    const uint32_t next = Imx28ApplySct(op, ChannelReg(n), value);
    c.toggle     = (next & kChToggle) != 0u ? 1u : 0u;
    c.value      = next & kChValueMask;
    c.accumulate = (next & kChAccumulate) != 0u ? 1u : 0u;
    c.samples    = static_cast<uint8_t>((next >> kChNumShift) & kChNumMask);
}

void Imx28Lradc::WriteDelay(uint32_t d, Imx28SctOp op, uint32_t value) {
    const uint32_t next = Imx28ApplySct(op, delay_[d], value) & kDelayWritable;
    delay_[d] = next & ~kDelayKick;
    if ((next & kDelayKick) != 0u) {
        KickDelay(d);
        DriveLines(false);
    }
}

void Imx28Lradc::WriteRegister(uint32_t reg, Imx28SctOp op, uint32_t addr, uint32_t value) {
    switch (reg) {
        case kCtrl1:
            ctrl1_ = (Imx28ApplySct(op, ctrl1_, value) & kCtrl1Writable);
            DriveLines(false);
            return;
        case kCtrl2: {
            const uint32_t next = Imx28ApplySct(op, ctrl2_, value) & kCtrl2Writable;
            if ((next & kCtrl2Frozen) != 0u) {
                HaltUnsupportedAccess("WriteWord", addr, value);
            }
            ctrl2_ = next;
            return;
        }
        case kCtrl3: {
            const uint32_t next = Imx28ApplySct(op, ctrl3_, value) & kCtrl3Writable;
            const uint32_t both = kCtrl3PowerUp | kCtrl3PowerDown;
            if ((next & ~(kCtrl3Cycle | both)) != 0u || (next & both) == both) {
                HaltUnsupportedAccess("WriteWord", addr, value);
            }
            ctrl3_ = next;
            return;
        }
        case kCtrl4:
            ctrl4_ = Imx28ApplySct(op, ctrl4_, value);
            return;
        case kConversion: {
            conversion_ = Imx28ApplySct(op, conversion_, value) & kConversionWritable;
            return;
        }
        case kThreshold0:
        case kThreshold1: {
            const uint32_t i    = (reg - kThreshold0) / kRegStride;
            const uint32_t next = Imx28ApplySct(op, threshold_[i], value) & kThresholdWritable;
            if ((next & kThresholdEnable) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
            threshold_[i] = next;
            return;
        }
        default:
            break;
    }
    if (reg >= kCh0 && reg < kChEnd) {
        WriteChannel((reg - kCh0) / kRegStride, op, value);
        return;
    }
    if (reg >= kDelay0 && reg < kDelayEnd) {
        WriteDelay((reg - kDelay0) / kRegStride, op, value);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

uint32_t Imx28Lradc::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) {
        if (!HasSctAlias(a.reg_offset)) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    const uint32_t reg = a.reg_offset;
    if (reg == kCtrl0) return ctrl0_;
    RequireOperating("ReadWord", ctrl0_, addr, 0);
    switch (reg) {
        case kCtrl1:      return ctrl1_;
        case kCtrl2:      return ctrl2_;
        case kCtrl3:      return ctrl3_;
        case kCtrl4:      return ctrl4_;
        case kConversion: return conversion_;
        case kVersion:    return kVersionValue;
        case kStatus: {
            uint32_t raw = 0u;
            for (uint32_t i = 0; i < 3u; ++i) raw |= DetectorLevel(i) ? 1u << i : 0u;
            return kStatusPresent | raw;
        }
        case kThreshold0:
        case kThreshold1: return threshold_[(reg - kThreshold0) / kRegStride];
        default:          break;
    }
    if (reg >= kCh0 && reg < kChEnd) return ChannelReg((reg - kCh0) / kRegStride);
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx28Lradc::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kCtrl0) {
        WriteCtrl0(a.op, value);
        return;
    }
    RequireOperating("WriteWord", ctrl0_, addr, value);
    if (a.op != Imx28SctOp::Write && !HasSctAlias(a.reg_offset)) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    WriteRegister(a.reg_offset, a.op, addr, value);
}

void Imx28Lradc::SaveState(StateWriter& w) {
    w.Write("ctrl0", ctrl0_);
    w.Write("ctrl1", ctrl1_);
    w.Write("ctrl2", ctrl2_);
    w.Write("ctrl3", ctrl3_);
    w.Write("ctrl4", ctrl4_);
    w.Write("conversion", conversion_);
    w.Write("threshold", threshold_);
    w.Write("delay", delay_);
    w.Write("detect", detect_);
    timer_.Save(w);
    for (const Channel& c : ch_) {
        w.Write("value", c.value);
        w.Write("toggle", c.toggle);
        w.Write("accumulate", c.accumulate);
        w.Write("samples", c.samples);
    }
}

void Imx28Lradc::RestoreState(StateReader& r) {
    r.Read("ctrl0", ctrl0_);
    r.Read("ctrl1", ctrl1_);
    r.Read("ctrl2", ctrl2_);
    r.Read("ctrl3", ctrl3_);
    r.Read("ctrl4", ctrl4_);
    r.Read("conversion", conversion_);
    r.Read("threshold", threshold_);
    r.Read("delay", delay_);
    r.Read("detect", detect_);
    timer_.Restore(r);
    for (Channel& c : ch_) {
        r.Read("value", c.value);
        r.Read("toggle", c.toggle);
        r.Read("accumulate", c.accumulate);
        r.Read("samples", c.samples);
    }
}

REGISTER_SERVICE(Imx28Lradc);

}
