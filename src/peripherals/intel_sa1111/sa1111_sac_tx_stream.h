#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "sa1111_sbi.h"

#include <cstdint>
#include <vector>

class Sa1111SacFrameClock;
class StateReader;
class StateWriter;

class Sa1111SacTxStream : public Service {
public:
    using Service::Service;

    /* SA-1111 Developer's Manual §7.3.1: "FIFO buffers are 16 words deep". */
    static constexpr uint32_t kFifoDepth = 16u;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Rescale(uint64_t now, uint64_t cpu_hz, GuestCycleClock::Rate fs);
    void StopClock(uint64_t now);
    void Settle(uint64_t now);
    void SetGrant(uint64_t now, Sa1111Sbi::BusGrant grant);
    Sa1111Sbi::BusGrant Grant() const { return grant_; }

    void Run(uint64_t now);
    void Hold(uint64_t now);
    bool Running() const { return running_; }
    uint64_t Requests() const { return requests_; }

    void StartBlock(uint64_t now);
    bool Fill(uint64_t now, uint32_t threshold, uint32_t& words_left);
    bool DoneCycle(uint32_t threshold, uint32_t words_left, uint64_t& cycle) const;
    bool NextRequestCycle(uint32_t threshold, uint32_t words_left, uint64_t& cycle) const;
    uint64_t LandedCycle() const;
    void Clear(uint64_t now);
    void ClearUnderrun(uint64_t now);
    bool PushWord(uint64_t now);
    uint32_t Level(uint64_t now) const;
    bool     Underrun(uint64_t now) const;
    bool     ServiceRequest(uint64_t now, bool enabled, uint32_t threshold) const;
    uint32_t StatusBits(uint64_t now, bool enabled, uint32_t threshold) const;
    bool     NextServiceEdge(uint64_t now, uint32_t threshold, uint32_t words_left, bool rising,
                             uint64_t& cycle) const;
    bool     NextUnderrun(uint64_t now, uint32_t threshold, uint32_t words_left,
                          uint64_t& cycle) const;

    void Save(StateWriter& w, uint64_t now) const;
    void Restore(StateReader& r, uint64_t now);

private:
    /* SA-1111 Developer's Manual §7.2.2 (page 7-5): "Data transfers can be read or write bursts
       up to eight words long." */
    static constexpr uint32_t kMaxBurst = 8u;
    /* SA-1110 §10.8 (pages 10-65 / 10-66): release "SA-1110 asserts SDCKE 1 at time (t + 4*Tmem)",
       grant "SA-1110 asserts MBGNT at time (t + 2*Tmem)", "Tmem ... (twice the CPU clock period)". */
    static constexpr uint64_t kBusHandBackCycles = (4u + 2u) * 2u;

    struct Landing {
        uint64_t cycle = 0;
        bool     kept  = false;
    };

    struct Request {
        uint64_t issue    = 0;
        uint32_t burst[2] = {};
        uint32_t lost     = 0;
    };

    struct StepState {
        uint64_t pushed     = 0;
        uint64_t start      = 0;
        uint64_t requests   = 0;
        uint64_t last_base  = 0;
        uint32_t lost_seen  = 0;
        bool     overflowed = false;
        Request  last;
    };

    uint64_t Consumed(uint64_t now) const;
    uint64_t ConsumeCycle(uint64_t words) const;
    uint64_t RequestCycle(uint64_t at) const;
    uint32_t LostLanded(uint64_t now) const;
    bool     InFlightGap(uint64_t now) const;
    void     ResolveGap(uint64_t now);
    bool     RequestDue(uint64_t now, uint32_t threshold) const;
    bool     Step(uint32_t threshold, uint32_t& words_left, uint64_t limit, uint64_t now,
                  bool bounded, StepState& s, std::vector<Request>* plan = nullptr) const;
    void     Landings(uint64_t now, uint32_t threshold, uint32_t words_left,
                      std::vector<Landing>& out) const;

    static uint32_t Words(const Request& rq) { return rq.burst[0] + rq.burst[1]; }

    Sa1111SacFrameClock*    frames_ = nullptr;
    Request  last_;
    uint32_t lost_seen_   = 0;
    uint64_t last_base_   = 0;
    uint64_t start_cycle_ = 0;
    bool     resolved_    = true;
    bool     running_ = false;
    uint64_t skip_    = 0;
    uint64_t held_    = 0;
    uint64_t pushed_  = 0;
    uint64_t start_   = 0;
    uint64_t requests_ = 0;
    bool     underrun_ = false;
    Sa1111Sbi::BusGrant grant_ = Sa1111Sbi::BusGrant::Stalled;
    bool     stalled_request_ = false;
};
