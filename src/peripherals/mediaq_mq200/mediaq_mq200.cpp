#include "mediaq_mq200.h"

#include "mediaq_mq200_config.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace {

constexpr uint32_t kCc00R         = 0x2000u;
constexpr uint32_t kCc01R         = 0x2004u;
constexpr uint32_t kCc00Reset     = 0x30u;
constexpr uint32_t kCc00Reserved  = 0xFFFFFF47u;

}

bool MediaQMq200::ShouldRegister() {
    return emu_.TryGet<MediaQMq200Config>() != nullptr;
}

void MediaQMq200::OnReady() {
    auto& cfg  = emu_.Get<MediaQMq200Config>();
    mmio_base_ = cfg.HostWindowBase();
    power_.Configure(cfg.OscillatorHz(), cfg.BusInterfaceMode());
    fb_.assign(kFbSize, 0u);
    AttachScanClock();
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        ResetChipLocked();
    }
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        if (kind == ResetLineKind::Rtc) OnPowerOnReset();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void MediaQMq200::ResetChipLocked() {
    std::fill(fb_.begin(), fb_.end(), static_cast<uint8_t>(0u));
    power_.PowerOnReset();
    memory_.PowerOnReset();
    in_.PowerOnReset();
    gc_.PowerOnReset();
    fp_.PowerOnReset();
    cc00_ = kCc00Reset;
    ge_.PowerOnReset();
    ge_window_.PowerOnReset();
    ge_powered_      = false;
    gc1_scan_        = Gc1Scan::Off;
    gc1_held_vcount_ = 0u;
    gc1_applied_     = MediaQMq200Gc::ScanTiming{};
    RefreshGatesLocked();
}

void MediaQMq200::OnPowerOnReset() {
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        CatchUpLocked(ScanNow());
        StopScanLocked();
        ResetChipLocked();
    }
    Settle(false);
}

bool MediaQMq200::PartialWriteAllowed(uint32_t roff) {
    return !MediaQMq200GeWindow::Owns(roff) && !MediaQMq200Interrupts::Owns(roff) &&
           roff != MediaQMq200Memory::kMm03R;
}

void MediaQMq200::RefreshGatesLocked() {
    fb_blocked_ = memory_.MiuBlocked();
    ge_blocked_ = power_.GeBlocked();
    if (!ge_blocked_) ge_blocked_ = fb_blocked_;
}

bool MediaQMq200::FbReadServed(uint32_t addr) const {
    if (!fb_blocked_) return true;
    if (power_.InD3() || memory_.MiuPoweredDown()) return false;
    emu_.Get<Fatal>().Die("MediaQMq200: frame buffer access at 0x%08X while %s", addr,
                          fb_blocked_);
}

void MediaQMq200::CheckGc1ObservedLocked() const {
    if (gc1_scan_ == Gc1Scan::Unmodeled && in_.Gc1VdeFallingUnmasked()) {
        emu_.Get<Fatal>().Die("MediaQMq200: IN01R unmasks the GC1 VDE edge while GC1 timing is "
                              "unmodeled: %s", gc1_applied_.why);
    }
}

uint32_t MediaQMq200::RegRead(uint32_t roff) {
    if (MediaQMq200GeWindow::Owns(roff)) {
        if (power_.InD3()) return 0u;
        return ge_window_.Read(roff, ge_blocked_);
    }
    std::lock_guard<std::mutex> lk(state_mtx_);
    return RegReadLocked(roff);
}

uint32_t MediaQMq200::RegReadLocked(uint32_t roff) {
    if (power_.InD3() && !MediaQMq200Power::D3Readable(roff)) return 0u;
    if (MediaQMq200Power::Owns(roff)) return power_.Read(roff);
    if (roff == kCc00R) return cc00_;
    if (roff == kCc01R) {
        if (ge_blocked_) {
            emu_.Get<Fatal>().Die("MediaQMq200: CC01R read while %s", ge_blocked_);
        }
        ge_.FlushPending();
        return ge_.StatusReady();
    }
    if (MediaQMq200Memory::Owns(roff)) return memory_.Read(roff);
    if (MediaQMq200Interrupts::Owns(roff)) {
        CatchUpLocked(ScanNow());
        return in_.Read(roff);
    }
    if (roff == MediaQMq200Gc::kGc1VCount) return Gc1VerticalCountLocked();
    if (roff == MediaQMq200Gc::kGc2VCount) {
        emu_.Get<Fatal>().Die("MediaQMq200: GC27R read; GC2 timing is not modeled");
    }
    if (MediaQMq200Gc::Owns(roff)) return gc_.Read(roff);
    if (MediaQMq200Fp::Owns(roff)) return fp_.Read(roff);
    emu_.Get<Fatal>().Die("MediaQMq200: read of undecoded register 0x%05X", roff);
}

void MediaQMq200::RegWrite(uint32_t roff, uint32_t value) {
    if (MediaQMq200GeWindow::Owns(roff) && !power_.InD3()) {
        if (!memory_.MiuPoweredDown()) ge_window_.Write(roff, value, ge_blocked_);
        return;
    }
    bool rescale = false;
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        rescale = RegWriteLocked(roff, value);
    }
    Settle(rescale);
}

bool MediaQMq200::SettleLocked() {
    const bool ge_powered = power_.GePowered();
    if (ge_powered_ && !ge_powered) ge_.PowerDownReset();
    ge_powered_ = ge_powered;
    RefreshGatesLocked();
    return UpdateGc1ScanLocked();
}

void MediaQMq200::Settle(bool rescale) {
    if (rescale) OnScanSourceClockChange();
    PublishScreenSizeOnEnableEdge();
}

bool MediaQMq200::FbWriteServed(uint32_t addr, uint32_t value, uint32_t width) {
    if (!fb_blocked_) return true;
    if (!power_.InD3()) {
        if (memory_.MiuPoweredDown()) return false;
        emu_.Get<Fatal>().Die("MediaQMq200: frame buffer write at 0x%08X while %s", addr,
                              fb_blocked_);
    }
    if (width != 4u) return false;
    bool rescale = false;
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        power_.LatchD3BusWrite(value);
        rescale = SettleLocked();
    }
    Settle(rescale);
    return false;
}

bool MediaQMq200::RegWriteLocked(uint32_t roff, uint32_t value) {
    if (MediaQMq200Interrupts::Owns(roff) && !power_.InD3()) {
        CatchUpLocked(ScanNow());
        in_.Write(roff, value);
        if (roff == MediaQMq200Interrupts::kIn01R) CheckGc1ObservedLocked();
        return false;
    }
    if (power_.InD3() && !MediaQMq200Power::D3Writable(roff)) {
        power_.LatchD3BusWrite(value);
    } else if (MediaQMq200Power::Owns(roff)) {
        power_.Write(roff, value);
    } else if (roff == kCc00R) {
        if (value & kCc00Reserved) {
            emu_.Get<Fatal>().Die("MediaQMq200: CC00R 0x%08X sets reserved bits", value);
        }
        cc00_ = value;
    } else if (MediaQMq200Memory::Owns(roff)) {
        memory_.Write(roff, value);
    } else if (MediaQMq200Gc::Owns(roff)) {
        gc_.Write(roff, value);
    } else if (MediaQMq200Fp::Owns(roff)) {
        fp_.Write(roff, value);
    } else {
        emu_.Get<Fatal>().Die("MediaQMq200: write 0x%08X to undecoded or read-only register "
                              "0x%05X", value, roff);
    }
    return SettleLocked();
}

bool MediaQMq200::UpdateGc1ScanLocked() {
    const MediaQMq200Gc::ScanTiming t = gc_.Gc1ScanTiming(power_);
    const uint64_t now = ScanNow();
    if (!t.on) {
        CatchUpLocked(now);
        StopScanLocked();
        gc1_scan_        = Gc1Scan::Off;
        gc1_held_vcount_ = 0u;
        return false;
    }
    switch (gc1_scan_) {
        case Gc1Scan::Off:
        case Gc1Scan::Waiting:
            if (!t.running) {
                gc1_scan_ = Gc1Scan::Waiting;
                return false;
            }
            gc1_applied_ = t;
            if (t.why) {
                gc1_scan_ = Gc1Scan::Unmodeled;
                CheckGc1ObservedLocked();
                return false;
            }
            StartScanLocked(now);
            gc1_scan_ = Gc1Scan::Live;
            return false;
        case Gc1Scan::Live:
            if (!t.running) {
                gc1_held_vcount_ =
                    static_cast<uint32_t>(ScanTickInFrameLocked(now) / gc1_applied_.line);
                PauseScanLocked(now);
                gc1_scan_ = Gc1Scan::Held;
                return false;
            }
            break;
        case Gc1Scan::Held:
            if (!t.running) return false;
            break;
        case Gc1Scan::Unmodeled:
            if (t.running && !t.why) {
                emu_.Get<Fatal>().Die("MediaQMq200: GC1 timing becomes modeled after the "
                                      "controller ran at an unmodeled rate");
            }
            if (t.why) gc1_applied_.why = t.why;
            return false;
    }
    if (t.why) {
        emu_.Get<Fatal>().Die("MediaQMq200: GC1 timing turns unmodeled while its scan runs: %s",
                              t.why);
    }
    if (t.line != gc1_applied_.line || t.frame != gc1_applied_.frame || t.vde != gc1_applied_.vde) {
        CatchUpLocked(now);
        gc1_applied_ = t;
        StartScanLocked(now);
        gc1_scan_ = Gc1Scan::Live;
        return false;
    }
    const bool rescale = gc1_scan_ == Gc1Scan::Live && !(t.rate == gc1_applied_.rate);
    gc1_applied_       = t;
    if (gc1_scan_ == Gc1Scan::Held) {
        UnpauseScanLocked(now);
        gc1_scan_ = Gc1Scan::Live;
    }
    return rescale;
}

void MediaQMq200::RestoreGc1WhyLocked() {
    gc1_applied_.why = gc_.Gc1ScanTiming(power_).why;
    if (gc1_scan_ == Gc1Scan::Unmodeled && !gc1_applied_.why) {
        gc1_applied_.why = "GC1 ran at an unmodeled rate before the restored save";
    }
}

uint32_t MediaQMq200::Gc1VerticalCountLocked() {
    switch (gc1_scan_) {
        case Gc1Scan::Live:
            return static_cast<uint32_t>(ScanTickInFrameLocked(ScanNow()) / gc1_applied_.line);
        case Gc1Scan::Held:
            return gc1_held_vcount_;
        case Gc1Scan::Unmodeled:
            emu_.Get<Fatal>().Die("MediaQMq200: GC07R read while GC1 timing is unmodeled: %s",
                                  gc1_applied_.why);
        default:
            return 0u;
    }
}

RasterScanPeripheral::ScanShape MediaQMq200::ScanShapeLocked() const {
    const MediaQMq200Gc::ScanTiming t = gc_.Gc1ScanTiming(power_);
    if (!t.running || t.why) {
        emu_.Get<Fatal>().Die("MediaQMq200: GC1 scan shape needs a running, modeled pixel clock");
    }
    ScanShape s;
    s.tick          = t.rate;
    s.frame.ticks   = t.frame;
    s.frame.edges   = 1u;
    s.frame.edge[0] = t.vde;
    return s;
}

bool MediaQMq200::EdgeRaisesInterruptLocked(uint32_t, bool) const {
    return false;
}

bool MediaQMq200::FrameEdgesInertLocked() const {
    return !in_.Gc1VdeFallingUnmasked();
}

void MediaQMq200::FrameEdgeLocked(uint32_t) {
    in_.OnGc1VdeFalling();
}

void MediaQMq200::PublishScreenSizeOnEnableEdge() {
    const bool on = IsEnabled();
    const uint32_t w = on ? GetGuestW() : 0u;
    const uint32_t h = on ? GetGuestH() : 0u;
    if (!mode_latch_.Publish(emu_, on, w, h)) return;
    LOG(Lcd, "MediaQMq200: display enabled %ux%u %ubpp stride=%u fb_off=0x%X\n",
        w, h, Bpp(), Stride(), FbWindowOffset());
}

uint8_t MediaQMq200::ReadByte(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off < kFbSize) return FbReadServed(addr) ? fb_[off] : 0u;
    if (off >= kRegWinOff && off < kRegWinOff + kRegSize) {
        const uint32_t roff = off - kRegWinOff;
        return static_cast<uint8_t>(RegRead(roff & ~0x3u) >> ((roff & 0x3u) * 8u));
    }
    HaltUnsupportedAccess("MQ200 ReadByte", addr, 0);
}

uint16_t MediaQMq200::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off + 2u <= kFbSize) return FbReadServed(addr) ? cerf::le::U16(fb_.data(), off) : 0u;
    if (off >= kRegWinOff && off < kRegWinOff + kRegSize && (off & 1u) == 0u) {
        const uint32_t roff = off - kRegWinOff;
        return static_cast<uint16_t>(RegRead(roff & ~0x3u) >> ((roff & 0x2u) * 8u));
    }
    HaltUnsupportedAccess("MQ200 ReadHalf", addr, 0);
}

uint32_t MediaQMq200::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off + 4u <= kFbSize) return FbReadServed(addr) ? cerf::le::U32(fb_.data(), off) : 0u;
    if (off >= kRegWinOff && off < kRegWinOff + kRegSize && (off & 3u) == 0u) {
        return RegRead(off - kRegWinOff);
    }
    HaltUnsupportedAccess("MQ200 ReadWord", addr, 0);
}

void MediaQMq200::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - MmioBase();
    if (off < kFbSize) {
        if (FbWriteServed(addr, value, 1u)) fb_[off] = value;
        return;
    }
    if (off >= kRegWinOff && off < kRegWinOff + kRegSize &&
        PartialWriteAllowed((off - kRegWinOff) & ~0x3u)) {
        if (power_.InD3()) return;
        const uint32_t roff  = off - kRegWinOff;
        const uint32_t shift = (roff & 0x3u) * 8u;
        const uint32_t word  = RegRead(roff & ~0x3u);
        RegWrite(roff & ~0x3u,
                 (word & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift));
        return;
    }
    HaltUnsupportedAccess("MQ200 WriteByte", addr, value);
}

void MediaQMq200::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - MmioBase();
    if (off + 2u <= kFbSize) {
        if (FbWriteServed(addr, value, 2u)) cerf::le::Put16(fb_.data() + off, value);
        return;
    }
    if (off >= kRegWinOff && off < kRegWinOff + kRegSize && (off & 1u) == 0u &&
        PartialWriteAllowed((off - kRegWinOff) & ~0x3u)) {
        if (power_.InD3()) return;
        const uint32_t roff  = off - kRegWinOff;
        const uint32_t shift = (roff & 0x2u) * 8u;
        const uint32_t word  = RegRead(roff & ~0x3u);
        RegWrite(roff & ~0x3u,
                 (word & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(value) << shift));
        return;
    }
    HaltUnsupportedAccess("MQ200 WriteHalf", addr, value);
}

void MediaQMq200::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off + 4u <= kFbSize) {
        if (FbWriteServed(addr, value, 4u)) cerf::le::Put32(fb_.data() + off, value);
        return;
    }
    if (off >= kRegWinOff && off < kRegWinOff + kRegSize && (off & 3u) == 0u) {
        RegWrite(off - kRegWinOff, value);
        return;
    }
    HaltUnsupportedAccess("MQ200 WriteWord", addr, value);
}

void MediaQMq200::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mtx_);
    w.WriteBytes("fb", fb_.data(), fb_.size());
    power_.SaveState(w);
    memory_.SaveState(w);
    in_.SaveState(w);
    gc_.SaveState(w);
    fp_.SaveState(w);
    w.Write("cc00", cc00_);
    ge_window_.SaveState(w);
    mode_latch_.SaveState(w);
    ge_.SaveState(w);
    SaveScanLocked(w);
    w.Write<uint8_t>("gc1_scan", static_cast<uint8_t>(gc1_scan_));
    w.Write("gc1_held_vcount", gc1_held_vcount_);
    w.Write<uint8_t>("gc1_applied_on", gc1_applied_.on ? 1u : 0u);
    w.Write<uint8_t>("gc1_applied_running", gc1_applied_.running ? 1u : 0u);
    w.Write("gc1_applied_rate_num", gc1_applied_.rate.num);
    w.Write("gc1_applied_rate_den", gc1_applied_.rate.den);
    w.Write("gc1_applied_line", gc1_applied_.line);
    w.Write("gc1_applied_frame", gc1_applied_.frame);
    w.Write("gc1_applied_vde", gc1_applied_.vde);
}

void MediaQMq200::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mtx_);
    r.ReadBytes("fb", fb_.data(), fb_.size());
    power_.RestoreState(r);
    memory_.RestoreState(r);
    in_.RestoreState(r);
    gc_.RestoreState(r);
    fp_.RestoreState(r);
    r.Read("cc00", cc00_);
    ge_window_.RestoreState(r);
    mode_latch_.RestoreState(r);
    ge_.RestoreState(r);
    RestoreScanLocked(r);
    uint8_t mode = 0;
    r.Read("gc1_scan", mode);
    gc1_scan_ = static_cast<Gc1Scan>(mode);
    r.Read("gc1_held_vcount", gc1_held_vcount_);
    uint8_t on      = 0;
    uint8_t running = 0;
    r.Read("gc1_applied_on", on);
    r.Read("gc1_applied_running", running);
    gc1_applied_.on      = on != 0u;
    gc1_applied_.running = running != 0u;
    r.Read("gc1_applied_rate_num", gc1_applied_.rate.num);
    r.Read("gc1_applied_rate_den", gc1_applied_.rate.den);
    r.Read("gc1_applied_line", gc1_applied_.line);
    r.Read("gc1_applied_frame", gc1_applied_.frame);
    r.Read("gc1_applied_vde", gc1_applied_.vde);
}

void MediaQMq200::PostRestore() {
    std::lock_guard<std::mutex> lk(state_mtx_);
    ge_powered_ = power_.GePowered();
    RefreshGatesLocked();
    RestoreGc1WhyLocked();
    ResumeScanLocked(ScanNow());
}

REGISTER_SERVICE(MediaQMq200);
