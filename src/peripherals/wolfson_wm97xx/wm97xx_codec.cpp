#include "wm97xx_codec.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../execution/engine_bridge.h"
#include <cerfengine/host_request_channel.h>
#include <cerfcore/state/state_stream.h>

namespace {

/* Wolfson WM9705 page 51: with DEL 15 CR gives "sample rates of 8, 12, 24 or 48ks/s"; Cirrus Logic WM9713L
   Rev 4.0 Table 48 (page 71): DEL 1111 CR 00 "every six AC-Link frames" to 11 "every AC-Link frame". */
constexpr uint32_t kFastRateFrames[4] = {6u, 4u, 2u, 1u};
constexpr uint16_t kPndn       = 0x8000u;
constexpr uint16_t kPrpOnBits  = 0xC000u;
constexpr uint32_t kPrpShift   = 14u;
constexpr uint32_t kPrpOn      = 3u;
constexpr uint32_t kPrpWake    = 1u;
/* Wolfson WM9705 page 42 and Cirrus Logic WM9713L Rev 4.0 page 121 (index 78h): RPR bit 13. */
constexpr uint16_t kRpr        = 1u << 13;

}  // namespace

void Wm97xxCodec::OnReady() {
    host_requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    host_requests_->RegisterListener([this] { DrainPenEdges(); });
}

void Wm97xxCodec::SetPenPosition(uint16_t raw_x, uint16_t raw_y) {
    raw_x_.store(raw_x, std::memory_order_relaxed);
    raw_y_.store(raw_y, std::memory_order_relaxed);
}

void Wm97xxCodec::QueuePenEdge(bool down) {
    {
        std::lock_guard<std::mutex> lk(edge_mtx_);
        edges_.push_back(down);
    }
    host_requests_->Request();
}

void Wm97xxCodec::DrainPenEdges() {
    std::vector<bool> edges;
    {
        std::lock_guard<std::mutex> lk(edge_mtx_);
        edges.swap(edges_);
    }
    for (bool down : edges) {
        if (down != pen_down_) LOG(Periph, "[WM97xx] pen %s\n", down ? "DOWN" : "UP");
        SetPenDown(down);
    }
}

uint32_t Wm97xxCodec::FastRateFrames(uint32_t cr) { return kFastRateFrames[cr]; }

void Wm97xxCodec::SetPenDown(bool down) {
    if (down == pen_down_) return;
    pen_down_ = down;
    pen_line_(down);
    const uint64_t now  = emu_.Get<GuestCycleClock>().Cycles();
    const bool     woke = down && WakeOnPen(now);
    uint64_t frame = 0;
    if (!frames_->FrameAt(now, frame)) {
        digitiser_.ClearHistory(down);
        OnPenSignal(0u, now);
        return;
    }
    digitiser_.SetPen(frame + 1u, down);
    if (woke) Reconfigure(frame, false);
    OnPenSignal(frame + 1u, now);
    frames_->OnCodecStreamChange();
}

/* Wolfson WM9705 page 50: in standby with PRP 01 a pen down sets "PRP[1] ... to active (1) and the pen
   digitiser wakes up"; Cirrus Logic WM9713L Rev 4.0 Table 47 (page 70): PRP 01 "touchpanel digitiser
   wakes up (changes to state 11) on pen-down", 11 "Pen digitiser and pen detect enabled". */
bool Wm97xxCodec::DigitiserPowered() {
    const uint16_t d2  = Peek(kRegDigitiserPower);
    uint32_t       prp = d2 >> kPrpShift;
    if (prp == kPrpWake && pen_down_) {
        Poke(kRegDigitiserPower, static_cast<uint16_t>(d2 | kPrpOnBits));
        prp = kPrpOn;
    }
    return prp == kPrpOn;
}

/* Wolfson WM9705 page 50: on a pen down in standby PRP[1] is set "and the pen digitiser wakes up, toggles
   the PENDET pin, and wakes the AC link by toggling the SDATAIN line"; "If RPR is set then pen down will
   also automatically reset PR4 and wake up the codec". */
bool Wm97xxCodec::WakeOnPen(uint64_t now) {
    const uint16_t d2 = Peek(kRegDigitiserPower);
    if ((d2 >> kPrpShift) != kPrpWake) return false;
    Poke(kRegDigitiserPower, static_cast<uint16_t>(d2 | kPrpOnBits));
    if (!LinkPoweredDown()) return true;
    const bool restart = (d2 & kRpr) != 0u;
    if (restart) WarmReset();
    frames_->CodecWake(now, restart);
    return true;
}

void Wm97xxCodec::LinkStopped(uint64_t) { digitiser_.Clear(pen_down_); }

void Wm97xxCodec::LinkStarted(uint64_t frame) { Reconfigure(frame, false); }

/* Wolfson WM9705 page 42 (index 7Ah): PNDN 15 "Indicates whether Pen is Down", ADR[2:0] 14:12
   "Conversion Address", D[11:0] "ADC output result". */
uint16_t Wm97xxCodec::ResultWord(const Wm97xxDigitiser::Result& r) {
    const uint16_t data = static_cast<uint16_t>(ConversionData(r.tag) & kAdcMax);
    return static_cast<uint16_t>((r.pen_down ? kPndn : 0u) | ((r.tag & 7u) << 12) | data);
}

uint16_t Wm97xxCodec::ReadResultRegister(uint64_t frame, uint16_t held) {
    Wm97xxDigitiser::Result r;
    const uint16_t word = digitiser_.LastResult(frame, r) ? ResultWord(r) : held;
    digitiser_.ReadResult(frame);
    return word;
}

/* Wolfson WM9705 page 41 (index 76h): SLEN 3 "allows conversion results to be output onto chosen slot", SLT
   2-0; Table 25 (page 43) and Cirrus Logic WM9713L Rev 4.0 Table 51 (page 75): 000 = slot 5 to 110 = slot 11. */
bool Wm97xxCodec::SlotMode(uint16_t d76) { return (d76 & (1u << 3)) != 0u; }

uint8_t Wm97xxCodec::RouteSlot(uint16_t d76) {
    const uint8_t slt = static_cast<uint8_t>(d76 & 7u);
    return SlotMode(d76) && slt != 7u ? static_cast<uint8_t>(5u + slt) : 0u;
}

uint32_t Wm97xxCodec::DigitiserSlot() { return RouteSlot(Peek(kRegDigitiserSlot)); }

uint64_t Wm97xxCodec::SlotWordsBefore(uint32_t slot, uint64_t frames) {
    return digitiser_.SlotWordsBefore(slot, frames);
}

bool Wm97xxCodec::FrameOfSlotWord(uint32_t slot, uint64_t n, uint64_t& frame) {
    return digitiser_.FrameOfSlotWord(slot, n, frame);
}

uint16_t Wm97xxCodec::SlotWord(uint32_t slot, uint64_t n) {
    Wm97xxDigitiser::Result r;
    if (!digitiser_.SlotWord(slot, n, r)) {
        emu_.Get<Fatal>().Die("Wm97xxCodec: slot-%u word %llu is in no conversion burst", slot,
                              static_cast<unsigned long long>(n));
    }
    return ResultWord(r);
}

void Wm97xxCodec::PruneSlotWords(uint32_t slot, uint64_t frames) { digitiser_.Prune(slot, frames); }

void Wm97xxCodec::PostRestore() {
    if (!pen_down_) return;
    SetPenDown(false);
}

void Wm97xxCodec::SavePen(StateWriter& w) const {
    w.Write<uint8_t>("pen_down", pen_down_ ? 1u : 0u);
    digitiser_.Save(w);
}

void Wm97xxCodec::RestorePen(StateReader& r) {
    uint8_t down = 0;
    r.Read("pen_down", down);
    pen_down_ = down != 0u;
    digitiser_.Restore(r);
}
