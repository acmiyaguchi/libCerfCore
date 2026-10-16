#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "pr31x00_intc.h"

#include <array>
#include <cstdint>
#include <memory>

namespace cerfcore {

class StateReader;
class StateWriter;

class Pr31x00SibDmaTimeline {
public:
    virtual bool     Live() const                         = 0;
    virtual bool     Moving() const                       = 0;
    virtual uint64_t PointerTicks(uint64_t cycle) const   = 0;
    virtual uint64_t LastIncrement(uint64_t cycle) const  = 0;
    virtual uint64_t CycleOfTick(uint64_t tick) const     = 0;
    virtual void     FlushReceived()                      = 0;

protected:
    ~Pr31x00SibDmaTimeline() = default;
};

class Pr31x00SibCountInt : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Attach(uint32_t bit, Pr31x00SibDmaTimeline& timeline);
    void     Restart(uint32_t port);
    void     Latch(uint32_t port);
    void     Rearm(uint32_t port);

    void SaveState(uint32_t port, StateWriter& w);
    void RestoreState(uint32_t port, StateReader& r);

private:
    static constexpr uint32_t kPorts = 2u;

    class Channel final : public Pr31x00IntcTimedSource {
    public:
        Channel(Pr31x00SibCountInt& owner, uint32_t bit, Pr31x00SibDmaTimeline& timeline);

        void Settle() override;
        bool EdgeSinceClear() const override;
        void OnLatched() override;
        void OnStatusCleared() override;
        void OnPathChanged(bool open) override;

        void NextEdgeFromNow();
        void Rearm();
        void Latch();
        void Save(StateWriter& w) const;
        void Restore(StateReader& r);

    private:
        Pr31x00SibCountInt&     owner_;
        const uint32_t          bit_;
        Pr31x00SibDmaTimeline&  timeline_;
        GuestCycleClock::Event* event_     = nullptr;
        uint64_t                edge_tick_ = 0;
        bool                    open_      = false;
    };

    std::array<std::unique_ptr<Channel>, kPorts> ch_;
    uint32_t                                     count_ = 0;

    GuestCycleClock* clock_ = nullptr;
    Pr31x00Intc*     intc_  = nullptr;
};

}
