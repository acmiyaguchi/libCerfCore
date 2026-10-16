#include "pr31x00_lcd.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/host_presentation.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "pr31x00_clock.h"
#include "pr31x00_intc.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x10C00028u;

enum RegIndex : uint32_t {
    kCtl1  = 0,   /* $028 R/W */
    kCtl2  = 1,   /* $02C write-only */
    kCtl3  = 2,   /* $030 write-only */
    kCtl4  = 3,   /* $034 write-only */
};

/* Video Control 1 (§17.4.1): LINECNT[9:0]<31:22> R, LOADDLY<21>, BAUDVAL[4:0]<20:16>,
   VIDDONEVAL[6:0]<15:9>, ENFREEZEFRAME<8>, BITSEL[1:0]<7:6>, DISPSPLIT<5>, DISP8<4>,
   DFMODE<3>, INVVID<2>, DISPON<1>, ENVID<0>. */
constexpr uint32_t kCtl1Writable = 0x003FFFFFu;
constexpr uint32_t kLineCntShift = 22;
constexpr uint32_t kCtl1ResetKept = 0x001FFE00u;
constexpr uint32_t kCtl1Timing    = 0x001F0000u;
constexpr uint32_t kEnVid        = 1u << 0;
constexpr uint32_t kInvVid       = 1u << 2;
constexpr uint32_t kEnFreezeFrame = 1u << 8;
constexpr uint32_t kDisp8        = 1u << 4;
constexpr uint32_t kDispSplit    = 1u << 5;
constexpr uint32_t kBitSelShift  = 6;

/* BITSEL[1:0] (§17.4.1): $0 monochrome, $1 2-bit gray, $2 4-bit gray, $3 8-bit color. */
constexpr uint32_t kBitSel8BitColor = 3u;

/* Video Control 2 (§17.4.2): VIDRATE[9:0]<31:22>, HORZVAL[8:0]<20:12>, LINEVAL[9:0]<9:0>.
   HORZVAL = HorzSize/4 - 1, or HorzSize/8 - 1 for an 8-bit non-split LCD (§17.3.3).
   LINEVAL = lines - 1 for a non-split LCD. */
constexpr uint32_t kHorzValShift = 12;
constexpr uint32_t kHorzValMask  = 0x1FFu;
constexpr uint32_t kLineValMask  = 0x3FFu;
constexpr uint32_t kVidRateShift = 22;
constexpr uint32_t kVidRateMask  = 0x3FFu;

/* BAUDVAL[4:0] (§17.4.1) divides the video master clock into the CP clock:
   CP Rate = f_VIDCLK / (BAUDVAL*2 + 2). Line Rate = CP Rate / (VIDRATE+1) and
   Frame Rate = Line Rate / (LINEVAL+1) (§17.4.2). */
constexpr uint32_t kBaudValShift = 16;
constexpr uint32_t kBaudValMask  = 0x1Fu;

/* LCDINT, Interrupt Status 1 bit 31: "Issues an interrupt at the end of each
   video frame" (§8.3.1). Status set 0 == Interrupt Status 1. */
constexpr uint32_t kLcdIntSet = 0;
constexpr uint32_t kLcdInt    = 1u << 31;

/* Video Control 3 (§17.4.3): VIDBANK[31:20] concatenated with VIDBASEHI[19:4] form the
   address video data is fetched from; bits 3-0 reserved. */
constexpr uint32_t kFbAddrMask = 0xFFFFFFF0u;

/* Video Control 4 (§17.4.4): DFVAL[7:0]<31:24>, FRAMEMASKVAL[3:0]<23:20>,
   VIDBASELO<19:4>; bits 3-0 reserved. VIDBASELO starts the lower address counter,
   which feeds FIFOLO - unused on a non-split display (§17.3.6). DFVAL scales the DF
   toggle rate and only reaches the DF pin while DFMODE is set (§17.4.1). */
constexpr uint32_t kCtl4FrameMaskVal = 0x00F00000u;

/* Reserved bits: §17.4.2 CTL2 <21> and <11:10>; §17.4.3 CTL3 <3:0>; §17.4.4 CTL4 <3:0>. */
constexpr uint32_t kCtl2Reserved  = 0x00200C00u;
constexpr uint32_t kCtl3Reserved  = 0x0000000Fu;
constexpr uint32_t kCtl4Reserved  = 0x0000000Fu;

}  /* namespace */

bool Pr31x00Lcd::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

/* Video Control 1 RESET column: LINECNT, LOADDLY and ENFREEZEFRAME..ENVID 0, BAUDVAL and
   VIDDONEVAL X (§17.4.1 p17-19). */
void Pr31x00Lcd::OnReady() {
    intc_       = &emu_.Get<Pr31x00Intc>();
    clock_      = &emu_.Get<Pr31x00Clock>();
    cycles_     = &emu_.Get<GuestCycleClock>();
    load_event_ = cycles_->Add([this] { OnLoadEvent(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
    AttachScanClock();
    cycles_->RegisterRateListener([this] { RearmLoad(); });
    clock_->RegisterVideoClockListener([this] {
        {
            std::lock_guard<std::mutex> lk(state_mtx_);
            const uint64_t now = ScanNow();
            CatchUpLocked(now);
            SyncScanLocked(now);
        }
        ScanEdgesRan();
        OnScanSourceClockChange();
        RearmLoad();
    });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        std::lock_guard<std::mutex> lk(state_mtx_);
        StopScanLocked();
        reg_[kCtl1]         &= kCtl1ResetKept;
        held_linecnt_        = 0;
        frame_edge_pending_  = false;
        enabled_edge_        = false;
        cp_rate_changed_     = false;
        freeze_due_          = false;
        freeze_stopped_      = false;
        LoadTimingLocked();
    });
}

/* CP Rate = f_VIDCLK / (BAUDVAL*2 + 2) (§17.4.1 p17-19); Line Rate = CP Rate / (VIDRATE+1),
   Frame Rate = Line Rate / (LINEVAL+1) (§17.4.2 p17-21); LCDINT "at the end of each video
   frame" (§17.3.10 p17-18). One scan tick is one line. */
RasterScanPeripheral::ScanShape Pr31x00Lcd::ScanShapeLocked() const {
    const uint64_t baudval = (reg_[kCtl1] >> kBaudValShift) & kBaudValMask;
    const uint64_t line_vidclks = (baudval * 2u + 2u) * (uint64_t{vidrate_} + 1u);
    const GuestCycleClock::Rate vid = clock_->VideoClockRate();
    if (vid.num == 0u || vid.den > UINT64_MAX / line_vidclks) {
        emu_.Get<Fatal>().Die("Pr31x00Lcd: a %llu-VIDCLK line at %llu/%llu Hz cannot be scheduled",
                              static_cast<unsigned long long>(line_vidclks),
                              static_cast<unsigned long long>(vid.num),
                              static_cast<unsigned long long>(vid.den));
    }
    return ScanShape{{vid.num, line_vidclks * vid.den}, ScanFrameLocked()};
}

RasterScanClock::Frame Pr31x00Lcd::ScanFrameLocked() const {
    RasterScanClock::Frame f;
    f.ticks   = uint64_t{lineval_} + 1u;
    f.edge[0] = uint64_t{lineval_} + 1u;
    f.edges   = 1u;
    return f;
}

void Pr31x00Lcd::FrameEdgeLocked(uint32_t) { frame_edge_pending_ = true; }

void Pr31x00Lcd::ScanEdgesRan() {
    bool pending;
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        pending             = frame_edge_pending_;
        frame_edge_pending_ = false;
    }
    if (pending) intc_->SetPending(kLcdIntSet, kLcdInt);
}

/* ENVID "will enable the video logic" (§17.4.1 p17-20); ENVIDCLK gates VIDCLK (§6.3.1
   p6-6). */
bool Pr31x00Lcd::ScanWantedLocked() const {
    return (reg_[kCtl1] & kEnVid) != 0u && clock_->VideoClockRate().num != 0u && !freeze_stopped_;
}

void Pr31x00Lcd::SyncScanLocked(uint64_t now) {
    const bool wanted = ScanWantedLocked();
    if (wanted == ScanLiveLocked()) return;
    if (wanted) {
        LoadTimingLocked();
        StartScanLocked(now);
        ScheduleFreezeLocked(now);
        return;
    }
    held_linecnt_ = LineCntLocked(now);
    StopScanLocked();
    LoadTimingLocked();
    freeze_due_ = false;
}

/* §17.3.2 (p17-9): ENFREEZEFRAME "causes the video baud clock to stop and the video to stop
   shifting cleanly at the end of the current frame". */
void Pr31x00Lcd::ScheduleFreezeLocked(uint64_t now) {
    if ((reg_[kCtl1] & kEnFreezeFrame) == 0u || !ScanLiveLocked() || freeze_due_) return;
    const uint64_t frame = uint64_t{lineval_} + 1u;
    freeze_due_  = true;
    freeze_tick_ = (ScanTicksLocked(now) / frame + 1u) * frame;
    ArmLoadLocked();
}

/* "For each new frame, the Line Counter is preloaded with the LINEVAL(9:0) control register
   setting and counts each time a LOAD pulse is generated" (§17.3.4 p17-11). */
uint32_t Pr31x00Lcd::LineCntLocked(uint64_t now) const {
    if (!ScanLiveLocked()) return held_linecnt_;
    return lineval_ - static_cast<uint32_t>(ScanTickInFrameLocked(now));
}

void Pr31x00Lcd::LoadTimingLocked() {
    vidrate_     = (reg_[kCtl2] >> kVidRateShift) & kVidRateMask;
    lineval_     = reg_[kCtl2] & kLineValMask;
    lineval_due_ = false;
    cycles_->Disarm(load_event_);
}

/* §17.3.2 (p17-9): the Video Rate Counter "is preloaded with the VIDRATE[9:0] control register
   setting" and generates the LOAD pulse; §17.3.4 (p17-11): "For each new frame, the Line Counter
   is preloaded with the LINEVAL(9:0)". */
void Pr31x00Lcd::WriteTimingLocked(uint32_t value, uint64_t now) {
    if (ScanLiveLocked() && ((value >> kVidRateShift) & kVidRateMask) != vidrate_) {
        emu_.Get<Fatal>().Die("Pr31x00Lcd: VIDEO_CTL2 0x%08X changes VIDRATE of 0x%08X while the "
                              "video logic scans", value, reg_[kCtl2]);
    }
    reg_[kCtl2] = value;
    if (!ScanLiveLocked()) {
        LoadTimingLocked();
        return;
    }
    const uint64_t tick  = ScanTicksLocked(now);
    const uint64_t frame = uint64_t{lineval_} + 1u;
    if (!lineval_due_ && (value & kLineValMask) != lineval_) {
        lineval_due_  = true;
        lineval_tick_ = (tick / frame + 1u) * frame;
    }
    ArmLoadLocked();
}

void Pr31x00Lcd::ArmLoadLocked() {
    if (!ScanLiveLocked() || (!lineval_due_ && !freeze_due_)) {
        cycles_->Disarm(load_event_);
        return;
    }
    uint64_t tick = UINT64_MAX;
    if (lineval_due_) tick = lineval_tick_;
    if (freeze_due_ && freeze_tick_ < tick) tick = freeze_tick_;
    cycles_->Arm(load_event_, ScanCycleOfTickLocked(tick));
}

void Pr31x00Lcd::ApplyLoadsLocked(uint64_t now) {
    if (lineval_due_ && (!freeze_due_ || lineval_tick_ < freeze_tick_)) {
        const uint64_t at = ScanCycleOfTickLocked(lineval_tick_);
        if (at <= now) {
            CatchUpLocked(at);
            LoadTimingLocked();
            StartScanLocked(at);
            if (freeze_due_) freeze_tick_ = uint64_t{lineval_} + 1u;
        }
    }
    if (freeze_due_) {
        const uint64_t at = ScanCycleOfTickLocked(freeze_tick_);
        if (at <= now) {
            CatchUpLocked(at);
            held_linecnt_ = LineCntLocked(at);
            StopScanLocked();
            LoadTimingLocked();
            freeze_due_     = false;
            freeze_stopped_ = true;
            return;
        }
    }
    CatchUpLocked(now);
    ArmLoadLocked();
}

void Pr31x00Lcd::OnLoadEvent() {
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        ApplyLoadsLocked(ScanNow());
    }
    ScanEdgesRan();
}

void Pr31x00Lcd::RearmLoad() {
    std::lock_guard<std::mutex> lk(state_mtx_);
    ArmLoadLocked();
}

bool Pr31x00Lcd::IsEnabled() const { return (reg_[kCtl1] & kEnVid) != 0; }
bool Pr31x00Lcd::IsInverted() const { return (reg_[kCtl1] & kInvVid) != 0; }

uint32_t Pr31x00Lcd::GetFbPa() const { return reg_[kCtl3] & kFbAddrMask; }

uint32_t Pr31x00Lcd::GetGuestW() const {
    const uint32_t horzval = (reg_[kCtl2] >> kHorzValShift) & kHorzValMask;
    return (horzval + 1u) * ((reg_[kCtl1] & kDisp8) ? 8u : 4u);
}

uint32_t Pr31x00Lcd::GetGuestH() const { return (reg_[kCtl2] & kLineValMask) + 1u; }

uint32_t Pr31x00Lcd::GetBitsPerPixel() const {
    return 1u << ((reg_[kCtl1] >> kBitSelShift) & 3u);
}

uint32_t Pr31x00Lcd::ReadWord(uint32_t addr) {
    switch ((addr - kBase) / 4u) {
        case kCtl1: {
            uint32_t value;
            bool     edged;
            {
                std::lock_guard<std::mutex> lk(state_mtx_);
                const uint64_t now = ScanNow();
                edged = CatchUpLocked(now);
                value = (LineCntLocked(now) << kLineCntShift) | reg_[kCtl1];
            }
            if (edged) ScanEdgesRan();
            return value;
        }
        /* $034 is write-only (§17.4.4) but latches: nk.exe sub_91002514
           read-modify-writes it (v0[13] &= 0xFF0FFFFF). */
        case kCtl4: return reg_[kCtl4];
        default:
            /* Video Control 2 and 3 are write-only (§17.4.2, §17.4.3). */
            HaltUnsupportedAccess("PR31x00 LCD ReadWord", addr, 0);
    }
}

void Pr31x00Lcd::WriteWord(uint32_t addr, uint32_t value) {
    bool enabled_edge;
    bool cp_rate_changed;
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        const uint64_t now = ScanNow();
        CatchUpLocked(now);
        WriteRegLocked(addr, value, now);
        enabled_edge     = enabled_edge_;
        cp_rate_changed  = cp_rate_changed_;
        enabled_edge_    = false;
        cp_rate_changed_ = false;
    }
    ScanEdgesRan();
    if (cp_rate_changed) {
        OnScanSourceClockChange();
        RearmLoad();
    }
    if (enabled_edge) emu_.Get<HostPresentation>().PanelEnabled();
}

void Pr31x00Lcd::WriteRegLocked(uint32_t addr, uint32_t value, uint64_t now) {
    switch ((addr - kBase) / 4u) {
        case kCtl1: {
            if (value & kDispSplit) {
                HaltUnsupportedAccess("PR31x00 LCD DISPSPLIT", addr, value);
            }
            if (((value >> kBitSelShift) & 3u) == kBitSel8BitColor) {
                HaltUnsupportedAccess("PR31x00 LCD BITSEL 8-bit color", addr, value);
            }
            const bool was_live   = ScanLiveLocked();
            const bool cp_changed = ((reg_[kCtl1] ^ value) & kCtl1Timing) != 0u;
            const bool was_enabled = (reg_[kCtl1] & kEnVid) != 0u;
            reg_[kCtl1] = value & kCtl1Writable;
            if ((value & kEnFreezeFrame) == 0u) {
                freeze_due_     = false;
                freeze_stopped_ = false;
            }
            SyncScanLocked(now);
            ScheduleFreezeLocked(now);
            if (was_live && ScanLiveLocked() && cp_changed) cp_rate_changed_ = true;
            /* Fire only on the ENVID 0->1 edge: the OAL programs VIDEO_CTL2's
               HORZVAL/LINEVAL before it sets ENVID (nk.exe sub_9F434078), so an
               earlier or per-write call would size the window from a stale CTL2. */
            if (!was_enabled && (reg_[kCtl1] & kEnVid)) enabled_edge_ = true;
            return;
        }

        case kCtl2: {
            if (value & kCtl2Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL2 reserved", addr, value);
            }
            WriteTimingLocked(value, now);
            return;
        }

        case kCtl3:
            if (value & kCtl3Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL3 reserved", addr, value);
            }
            reg_[kCtl3] = value;
            return;

        case kCtl4:
            if (value & kCtl4Reserved) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL4 reserved", addr, value);
            }
            if (value & kCtl4FrameMaskVal) {
                HaltUnsupportedAccess("PR31x00 LCD VIDEO_CTL4 FRAMEMASKVAL", addr, value);
            }
            reg_[kCtl4] = value;
            return;

        default:
            HaltUnsupportedAccess("PR31x00 LCD WriteWord", addr, value);
    }
}

void Pr31x00Lcd::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mtx_);
    for (uint32_t i = 0; i < kRegs; ++i) w.Write("reg", reg_[i]);
    w.Write("held_linecnt", held_linecnt_);
    SaveScanLocked(w);
    w.Write("vidrate", vidrate_);
    w.Write("lineval", lineval_);
    w.Write<uint8_t>("lineval_due", lineval_due_ ? 1u : 0u);
    w.Write("lineval_tick", lineval_tick_);
    w.Write<uint8_t>("freeze_due", freeze_due_ ? 1u : 0u);
    w.Write<uint8_t>("freeze_stopped", freeze_stopped_ ? 1u : 0u);
    w.Write("freeze_tick", freeze_tick_);
}

void Pr31x00Lcd::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mtx_);
    for (uint32_t i = 0; i < kRegs; ++i) r.Read("reg", reg_[i]);
    r.Read("held_linecnt", held_linecnt_);
    RestoreScanLocked(r);
    uint8_t lineval_due = 0;
    r.Read("vidrate", vidrate_);
    r.Read("lineval", lineval_);
    r.Read("lineval_due", lineval_due);
    r.Read("lineval_tick", lineval_tick_);
    uint8_t freeze_due = 0, freeze_stopped = 0;
    r.Read("freeze_due", freeze_due);
    r.Read("freeze_stopped", freeze_stopped);
    r.Read("freeze_tick", freeze_tick_);
    freeze_due_         = freeze_due != 0u;
    freeze_stopped_     = freeze_stopped != 0u;
    lineval_due_        = lineval_due != 0u;
    frame_edge_pending_ = false;
    enabled_edge_       = false;
    cp_rate_changed_    = false;
    cycles_->Disarm(load_event_);
}

void Pr31x00Lcd::PostRestore() {
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        if (ScanWantedLocked()) ResumeScanLocked(ScanNow());
        ArmLoadLocked();
    }
    ScanEdgesRan();
}

REGISTER_SERVICE(Pr31x00Lcd);

}
