#include "pr31x00_sib_count_int.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "pr31500_id.h"
#include "pr31700_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Interrupt Status 1 (TMPR3911 §8.3.1, printed p.8-5). */
constexpr uint32_t kStatus1 = 0;

constexpr uint64_t kNoEdge = UINT64_MAX;

}

bool Pr31x00SibCountInt::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00SibCountInt::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    intc_  = &emu_.Get<Pr31x00Intc>();
}

uint32_t Pr31x00SibCountInt::Attach(uint32_t bit, Pr31x00SibDmaTimeline& timeline) {
    if (count_ >= kPorts) emu_.Get<Fatal>().Die("Pr31x00SibCountInt: a third DMA circuit attached");
    const uint32_t port = count_;
    ch_[port] = std::make_unique<Channel>(*this, bit, timeline);
    ++count_;
    intc_->RegisterTimedSource(kStatus1, bit, *ch_[port]);
    return port;
}

void Pr31x00SibCountInt::Restart(uint32_t port) {
    ch_[port]->NextEdgeFromNow();
    ch_[port]->Rearm();
}

void Pr31x00SibCountInt::Latch(uint32_t port) { ch_[port]->Latch(); }

void Pr31x00SibCountInt::Rearm(uint32_t port) { ch_[port]->Rearm(); }

void Pr31x00SibCountInt::SaveState(uint32_t port, StateWriter& w) { ch_[port]->Save(w); }

void Pr31x00SibCountInt::RestoreState(uint32_t port, StateReader& r) { ch_[port]->Restore(r); }

Pr31x00SibCountInt::Channel::Channel(Pr31x00SibCountInt& owner, uint32_t bit,
                                     Pr31x00SibDmaTimeline& timeline)
    : owner_(owner), bit_(bit), timeline_(timeline), edge_tick_(kNoEdge) {
    event_ = owner_.clock_->Add([this] { Latch(); });
}

void Pr31x00SibCountInt::Channel::Settle() { timeline_.FlushReceived(); }

/* SNDDMACNTINT / TELDMACNTINT: "Issues an interrupt each time the sound DMA buffer pointer is
   incremented, which occurs whenever a new sound sample is read from and/or written to the
   sound DMA buffer." (TMPR3911 §13.5, printed p.13-17) */
bool Pr31x00SibCountInt::Channel::EdgeSinceClear() const {
    if (edge_tick_ == kNoEdge || !timeline_.Moving()) return false;
    return timeline_.LastIncrement(owner_.clock_->Cycles()) >= edge_tick_;
}

void Pr31x00SibCountInt::Channel::OnLatched() {
    edge_tick_ = kNoEdge;
    owner_.clock_->Disarm(event_);
}

void Pr31x00SibCountInt::Channel::OnStatusCleared() {
    NextEdgeFromNow();
    Rearm();
}

void Pr31x00SibCountInt::Channel::OnPathChanged(bool open) {
    open_ = open;
    Rearm();
}

void Pr31x00SibCountInt::Channel::NextEdgeFromNow() {
    edge_tick_ = timeline_.Live() ? timeline_.PointerTicks(owner_.clock_->Cycles()) + 1u : kNoEdge;
}

void Pr31x00SibCountInt::Channel::Rearm() {
    if (open_ && edge_tick_ != kNoEdge && timeline_.Moving()) {
        owner_.clock_->Arm(event_, timeline_.CycleOfTick(edge_tick_));
    } else {
        owner_.clock_->Disarm(event_);
    }
}

void Pr31x00SibCountInt::Channel::Latch() {
    if (!EdgeSinceClear()) return;
    OnLatched();
    owner_.intc_->SetPending(kStatus1, bit_);
}

void Pr31x00SibCountInt::Channel::Save(StateWriter& w) const {
    w.Write("cnt_edge", edge_tick_);
}

void Pr31x00SibCountInt::Channel::Restore(StateReader& r) {
    r.Read("cnt_edge", edge_tick_);
    owner_.clock_->Disarm(event_);
}

REGISTER_SERVICE(Pr31x00SibCountInt);

}
