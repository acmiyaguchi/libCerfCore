#define NOMINMAX

#include "pr31x00_sib_audio.h"

#include "pr31x00_intc.h"
#include "pr31x00_sib_address_counter.h"
#include "pr31x00_sib_capture.h"
#include "pr31x00_sib_count_int.h"
#include "pr31x00_sib_speaker.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>
#include "../rated_tick_count.h"

#include <algorithm>
#include <cstdint>

namespace cerfcore {

namespace {

/* Interrupt Status 1 (TMPR3911 §8.3.1, printed p.8-5): SND0_5INT<22>, SND1_0INT<21>, TEL0_5INT<20>,
   TEL1_0INT<19>, SNDDMACNTINT<18>, TELDMACNTINT<17>. */
constexpr uint32_t kStatus1        = 0;
constexpr uint32_t kSnd0_5Int      = 1u << 22;
constexpr uint32_t kSnd1_0Int      = 1u << 21;
constexpr uint32_t kTel0_5Int      = 1u << 20;
constexpr uint32_t kTel1_0Int      = 1u << 19;
constexpr uint32_t kSndDmaCntInt   = 1u << 18;
constexpr uint32_t kTelDmaCntInt   = 1u << 17;

class Pr31x00SibAudioPlayer : public Pr31x00SibAudioSink {
public:
    using Pr31x00SibAudioSink::Pr31x00SibAudioSink;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const std::string_view soc = bd->GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }

    void OnReady() override {
        clock_   = &emu_.Get<GuestCycleClock>();
        mem_     = &emu_.Get<EmulatedMemory>();
        capture_ = &emu_.Get<Pr31x00SibCapture>();
        speaker_ = &emu_.Get<Pr31x00SibSpeaker>();
        count_   = &emu_.Get<Pr31x00SibCountInt>();
        sound_.Attach();
        telecom_.Attach();
        clock_->RegisterRateListener([this] {
            sound_.OnCoreRate();
            telecom_.OnCoreRate();
        });
        emu_.Get<Hibernation>().RegisterPreSaveListener([this] { FlushReceived(); });
    }

    void FlushReceived() override {
        sound_.FlushReceived();
        telecom_.FlushReceived();
    }

    void StartDma(Pr31x00SibCircuit circuit, const Pr31x00SibDmaSetup& dma) override {
        Of(circuit).Start(dma);
    }
    void UpdateDma(Pr31x00SibCircuit circuit, const Pr31x00SibDmaSetup& dma) override {
        Of(circuit).Update(dma);
    }
    void StopDma(Pr31x00SibCircuit circuit) override { Of(circuit).Stop(); }

    /* SNDDMAPTR[13:2] / TELDMAPTR[13:2] "provide the status of the sound / telecom DMA counter"
       (TMPR3911 §13.6.15, printed p.13-28 / 13-29). */
    uint32_t DmaPointer(Pr31x00SibCircuit circuit) override { return Of(circuit).Pointer(); }

    void SaveState(StateWriter& w) override {
        w.BeginFrame(static_cast<uint32_t>(Pr31x00SibCircuit::Sound));
        sound_.Save(w);
        w.EndFrame();
        w.BeginFrame(static_cast<uint32_t>(Pr31x00SibCircuit::Telecom));
        telecom_.Save(w);
        w.EndFrame();
    }

    void RestoreState(StateReader& r) override {
        speaker_->Abort();
        capture_->Clear();
        r.EnterFrame();
        sound_.Restore(r);
        r.LeaveFrame();
        r.EnterFrame();
        telecom_.Restore(r);
        r.LeaveFrame();
    }

private:
    /* TMPR3911 §13.4 (printed p.13-15): "Two identical circuits (one for sound DMA and one for
       telecom DMA)". */
    class Circuit final : public Pr31x00SibDmaTimeline {
    public:
        Circuit(Pr31x00SibAudioPlayer& owner, uint32_t half_int, uint32_t end_int, uint32_t cnt_int,
                bool host_audio)
            : p_(owner), half_int_(half_int), end_int_(end_int), cnt_int_(cnt_int),
              host_audio_(host_audio) {}

        void Attach() {
            event_    = p_.clock_->Add([this] { OnMatch(); });
            cnt_port_ = p_.count_->Attach(cnt_int_, *this);
        }

        bool Live() const override { return active_ && !ended_; }
        bool Moving() const override { return active_ && !ended_ && running_; }
        uint64_t PointerTicks(uint64_t cycle) const override { return WordPosition(cycle).ticks; }
        uint64_t CycleOfTick(uint64_t tick) const override { return words_.CycleOfTick(tick); }

        /* "For the one-time mode, the DMA logic will stop executing whenever the end-of-buffer is
           reached." (TMPR3911 §13.4, printed p.13-15) */
        uint64_t LastIncrement(uint64_t cycle) const override {
            const uint64_t t = words_.TicksAt(cycle);
            if (!dma_.one_time) return t;
            uint64_t end = next_tick_;
            uint32_t c   = CntAfter(cnt_, end - cnt_tick_);
            while (c != Size()) {
                const uint64_t k = NextMatch(c);
                c = CntAfter(c, k);
                end += k;
            }
            return std::min(t, end);
        }

        void Start(const Pr31x00SibDmaSetup& dma) {
            dma_         = dma;
            active_      = true;
            ended_       = false;
            cnt_         = 0;
            cnt_tick_    = 0;
            next_tick_   = 0;
            queued_tick_ = 0;
            rx_tick_     = 0;
            PlaceWords(p_.clock_->Cycles(), RatedTickCount::Position{});
            if (Plays()) {
                ApplyHostFormat();
                p_.speaker_->Begin();
            }
            if (Records()) OpenMic();
            QueueThrough(1u);
            OnMatch();
            p_.count_->Restart(cnt_port_);
        }

        void Update(const Pr31x00SibDmaSetup& dma) {
            const uint64_t now = p_.clock_->Cycles();
            DeliverDueMatches(now);
            p_.count_->Latch(cnt_port_);
            const RatedTickCount::Position at = WordPosition(now);
            if (!ended_) {
                WriteReceived(at.ticks);
                cnt_      = CntAfter(cnt_, at.ticks - cnt_tick_);
                cnt_tick_ = at.ticks;
            }
            const bool rate_changed = dma.frame_rate.Reduced() != dma_.frame_rate.Reduced() ||
                                      dma.sixteen_bit != dma_.sixteen_bit || dma.stereo != dma_.stereo;
            const bool out_started = host_audio_ && dma.tx && !dma_.tx;
            const bool out_stopped = host_audio_ && !dma.tx && dma_.tx;
            const bool rx_started  = dma.rx && !dma_.rx;
            const uint32_t was     = ended_ ? 0u : CompareBits(cnt_);
            dma_ = dma;
            const uint32_t rose = ended_ ? 0u : CompareBits(cnt_) & ~was;
            Raise(rose);
            if (dma_.one_time && (rose & end_int_) != 0u) {
                ended_ = true;
                if (Plays()) p_.speaker_->Finish();
            }
            if (rate_changed) PlaceWords(now, at);
            if (out_stopped) p_.speaker_->Finish();
            if (Plays() && (rate_changed || out_started)) ApplyHostFormat();
            if (out_started) {
                p_.speaker_->Begin();
                queued_tick_ = at.ticks + 1u;
            }
            if (rx_started) rx_tick_ = at.ticks + 1u;
            if (Records() && rx_started) OpenMic();
            else if (Records() && rate_changed) RequestMic();
            if (!ended_) {
                next_tick_ = cnt_tick_ + NextMatch(cnt_);
                if (out_started) QueueThrough(next_tick_ + 1u);
            }
            Arm();
            p_.count_->Rearm(cnt_port_);
        }

        void Stop() {
            const uint64_t now = p_.clock_->Cycles();
            DeliverDueMatches(now);
            p_.count_->Latch(cnt_port_);
            if (!ended_) {
                const uint64_t t = WordPosition(now).ticks;
                WriteReceived(t);
                cnt_ = CntAfter(cnt_, t - cnt_tick_);
            }
            active_ = false;
            p_.clock_->Disarm(event_);
            if (Plays()) p_.speaker_->Finish();
            p_.count_->Rearm(cnt_port_);
        }

        void FlushReceived() override {
            DeliverDueMatches(p_.clock_->Cycles());
            if (active_ && !ended_) WriteReceived(WordPosition(p_.clock_->Cycles()).ticks);
        }

        uint32_t Pointer() {
            FlushReceived();
            if (!active_ || ended_) return cnt_;
            return CntAfter(cnt_, WordPosition(p_.clock_->Cycles()).ticks - cnt_tick_);
        }

        void OnCoreRate() {
            if (!active_ || !running_) return;
            words_.Rescale(p_.clock_->Cycles(), p_.clock_->ClockRate(), WordRate());
            Arm();
            p_.count_->Rearm(cnt_port_);
        }

        void Save(StateWriter& w) {
            const RatedTickCount::Position at = WordPosition(p_.clock_->Cycles());
            w.Write<uint8_t>("active", active_ ? 1u : 0u);
            w.Write<uint8_t>("ended", ended_ ? 1u : 0u);
            w.Write<uint8_t>("tx", dma_.tx ? 1u : 0u);
            w.Write<uint8_t>("rx", dma_.rx ? 1u : 0u);
            w.Write<uint8_t>("rx_first", dma_.rx_first ? 1u : 0u);
            w.Write("tx_start_pa", dma_.tx_start_pa);
            w.Write("rx_start_pa", dma_.rx_start_pa);
            w.Write("size", dma_.size);
            w.Write<uint8_t>("one_time", dma_.one_time ? 1u : 0u);
            w.Write<uint8_t>("16bit", dma_.sixteen_bit ? 1u : 0u);
            w.Write<uint8_t>("stereo", dma_.stereo ? 1u : 0u);
            w.Write<uint8_t>("muted", dma_.muted ? 1u : 0u);
            w.Write("rate_num", dma_.frame_rate.num);
            w.Write("rate_den", dma_.frame_rate.den);
            w.Write("word", at.ticks);
            w.Write("phase", at.phase);
            w.Write("phase_den", at.phase_den);
            w.Write("cnt", cnt_);
            w.Write("cnt_tick", cnt_tick_);
            w.Write("next_tick", next_tick_);
            w.Write("rx_tick", rx_tick_);
            p_.count_->SaveState(cnt_port_, w);
        }

        void Restore(StateReader& r) {
            uint8_t active = 0, ended = 0, tx = 0, rx = 0, rx_first = 0;
            uint8_t one_time = 0, sixteen = 0, stereo = 0, muted = 0;
            RatedTickCount::Position at;
            r.Read("active", active);
            r.Read("ended", ended);
            r.Read("tx", tx);
            r.Read("rx", rx);
            r.Read("rx_first", rx_first);
            r.Read("tx_start_pa", dma_.tx_start_pa);
            r.Read("rx_start_pa", dma_.rx_start_pa);
            r.Read("size", dma_.size);
            r.Read("one_time", one_time);
            r.Read("16bit", sixteen);
            r.Read("stereo", stereo);
            r.Read("muted", muted);
            r.Read("rate_num", dma_.frame_rate.num);
            r.Read("rate_den", dma_.frame_rate.den);
            r.Read("word", at.ticks);
            r.Read("phase", at.phase);
            r.Read("phase_den", at.phase_den);
            r.Read("cnt", cnt_);
            r.Read("cnt_tick", cnt_tick_);
            r.Read("next_tick", next_tick_);
            r.Read("rx_tick", rx_tick_);
            p_.count_->RestoreState(cnt_port_, r);
            dma_.tx          = tx != 0u;
            dma_.rx          = rx != 0u;
            dma_.rx_first    = rx_first != 0u;
            dma_.one_time    = one_time != 0u;
            dma_.sixteen_bit = sixteen != 0u;
            dma_.stereo      = stereo != 0u;
            dma_.muted       = muted != 0u;
            active_          = active != 0u;
            ended_           = ended != 0u;

            p_.clock_->Disarm(event_);
            if (!active_) return;
            PlaceWords(p_.clock_->Cycles(), at);
            if (Plays()) {
                ApplyHostFormat();
                p_.speaker_->Begin();
            }
            if (Records()) RequestMic();
            if (ended_) return;
            queued_tick_ = at.ticks;
            QueueThrough(next_tick_ + 1u);
            Arm();
        }

    private:
        bool Plays() const { return host_audio_ && dma_.tx; }
        bool Records() const { return host_audio_ && dma_.rx; }

        void RequestMic() {
            if (dma_.frame_rate.num == 0u) return;
            p_.capture_->Request(static_cast<uint32_t>(dma_.frame_rate.RoundedHz()),
                                 static_cast<uint16_t>(Channels()));
        }

        void OpenMic() {
            RequestMic();
            p_.capture_->Clear();
        }

        Pr31x00SibAddressCounter Counter() const { return Pr31x00SibAddressCounter(dma_.size); }

        uint32_t Size() const { return Counter().Size(); }

        uint32_t Channels() const { return dma_.stereo ? 2u : 1u; }

        /* Tables 13.3.4 - 13.3.6 (printed p.13-13 / 13-14): a 16-bit word holds two frames, an
           8-bit word four, and a subframe-1 stereo frame takes both halves of that span. */
        uint64_t FramesPerWord() const { return (dma_.sixteen_bit ? 2u : 4u) / Channels(); }

        GuestCycleClock::Rate WordRate() const {
            return dma_.frame_rate.Scaled(1u, FramesPerWord());
        }

        uint32_t CntAfter(uint32_t c, uint64_t k) const { return Counter().After(c, k); }

        uint64_t NextMatch(uint32_t c) const { return Counter().NextMatch(c); }

        /* Figure 13.4.1 (printed p.13-16): CMP of ADDR CNT against BUFFER SIZE and BUFFER SIZE / 2. */
        uint32_t CompareBits(uint32_t c) const {
            return (c == Size() / 2u ? half_int_ : 0u) | (c == Size() ? end_int_ : 0u);
        }

        void Raise(uint32_t bits) {
            if (bits != 0u) p_.emu_.Get<Pr31x00Intc>().SetPending(kStatus1, bits);
        }

        RatedTickCount::Position WordPosition(uint64_t now) const {
            return running_ ? words_.PositionAt(now) : frozen_;
        }

        void PlaceWords(uint64_t now, const RatedTickCount::Position& at) {
            if (dma_.frame_rate.num == 0u) {
                running_ = false;
                frozen_  = at;
                return;
            }
            words_.SetRate(p_.clock_->ClockRate(), WordRate());
            words_.PlaceAt(now, at);
            running_ = true;
        }

        void ApplyHostFormat() {
            if (dma_.frame_rate.num == 0u) return;
            p_.speaker_->SetFormat(static_cast<uint32_t>(dma_.frame_rate.RoundedHz()),
                                   static_cast<uint16_t>(Channels()));
        }

        void Arm() {
            if (!active_ || ended_ || !running_) {
                p_.clock_->Disarm(event_);
                return;
            }
            p_.clock_->Arm(event_, words_.CycleOfTick(next_tick_));
        }

        /* SNDBUFF1TIME / TELBUFF1TIME: "the ... DMA controller will stop executing when it reaches
           the end of the DMA buffer" (TMPR3911 §13.6.15, printed p.13-28). */
        void OnMatch() {
            WriteReceived(next_tick_);
            cnt_      = CntAfter(cnt_, next_tick_ - cnt_tick_);
            cnt_tick_ = next_tick_;
            Raise(CompareBits(cnt_));
            if (dma_.one_time && cnt_ == Size()) {
                p_.count_->Latch(cnt_port_);
                ended_ = true;
                if (Plays()) p_.speaker_->Finish();
                p_.count_->Rearm(cnt_port_);
                return;
            }
            next_tick_ = cnt_tick_ + NextMatch(cnt_);
            QueueThrough(next_tick_ + 1u);
            Arm();
        }

        void DeliverDueMatches(uint64_t now) {
            while (active_ && !ended_ && running_ && words_.TicksAt(now) >= next_tick_) OnMatch();
        }

        /* SNDRXHOLD: "transparently written to the sound RX DMA buffer from this register"
           (TMPR3911 §13.6.8, printed p.13-25); the RX buffer starts at SNDRXSTART / TELRXSTART
           (§13.6.2, §13.6.4). */
        void WriteReceived(uint64_t through_tick) {
            if (!dma_.rx) return;
            if (rx_tick_ < cnt_tick_) rx_tick_ = cnt_tick_;
            for (; rx_tick_ <= through_tick; ++rx_tick_) {
                p_.mem_->WriteWord(dma_.rx_start_pa + CntAfter(cnt_, rx_tick_ - cnt_tick_) * 4u,
                                   ReceivedWord());
            }
        }

        uint32_t ReceivedWord() {
            if (!host_audio_) return p_.capture_->TelecomReceived();
            return p_.capture_->Received(rx_tick_, dma_.sixteen_bit);
        }

        uint32_t AheadWord(uint64_t t) {
            return p_.capture_->Ahead(t, rx_tick_, dma_.sixteen_bit);
        }

        /* SNDDMALOOP set: "the sound DMA controller issues RX DMA requests first, followed by TX
           DMA requests. This ordering allows an RX-to-TX immediate loopback via the DMA buffer"
           (TMPR3911 §13.6.15, printed p.13-28). */
        bool TransmitsReceived() const {
            return dma_.rx && dma_.rx_first && dma_.rx_start_pa == dma_.tx_start_pa;
        }

        void QueueThrough(uint64_t end_tick) {
            if (!Plays()) return;
            if (queued_tick_ < cnt_tick_) queued_tick_ = cnt_tick_;
            if (end_tick <= queued_tick_) return;
            for (uint64_t t = queued_tick_; t < end_tick; ++t) {
                const uint32_t pa = dma_.tx_start_pa + CntAfter(cnt_, t - cnt_tick_) * 4u;
                const uint32_t w  = TransmitsReceived() && t >= rx_tick_ ? AheadWord(t)
                                                                          : p_.mem_->ReadWord(pa);
                /* ENSF0: "causing the SIB serial transmitted data during this subframe to be
                   zeroed" (TMPR3911 §13.6.6, printed p.13-24); philips_nino_300 wavedev.dll
                   sub_1871F50 stores each 16-bit sample byte-swapped. */
                p_.speaker_->AddWord(dma_.muted ? 0u : cerf::ByteSwap32(w), dma_.sixteen_bit);
            }
            queued_tick_ = end_tick;
            p_.speaker_->Flush();
        }

        Pr31x00SibAudioPlayer&   p_;
        const uint32_t           half_int_;
        const uint32_t           end_int_;
        const uint32_t           cnt_int_;
        const bool               host_audio_;
        GuestCycleClock::Event*  event_    = nullptr;
        uint32_t                 cnt_port_ = 0;
        RatedTickCount           words_;
        RatedTickCount::Position frozen_;
        Pr31x00SibDmaSetup       dma_;
        uint32_t                 cnt_         = 0;
        uint64_t                 cnt_tick_    = 0;
        uint64_t                 next_tick_   = 0;
        uint64_t                 queued_tick_ = 0;
        uint64_t                 rx_tick_     = 0;
        bool                     active_      = false;
        bool                     ended_       = false;
        bool                     running_     = false;
    };

    Circuit& Of(Pr31x00SibCircuit circuit) {
        return circuit == Pr31x00SibCircuit::Sound ? sound_ : telecom_;
    }

    GuestCycleClock*     clock_   = nullptr;
    EmulatedMemory*      mem_     = nullptr;
    Pr31x00SibCapture*   capture_ = nullptr;
    Pr31x00SibSpeaker*   speaker_ = nullptr;
    Pr31x00SibCountInt*  count_   = nullptr;
    Circuit              sound_{*this, kSnd0_5Int, kSnd1_0Int, kSndDmaCntInt, true};
    Circuit              telecom_{*this, kTel0_5Int, kTel1_0Int, kTelDmaCntInt, false};
};

}

REGISTER_SERVICE_AS(Pr31x00SibAudioPlayer, Pr31x00SibAudioSink);

}
