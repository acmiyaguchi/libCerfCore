#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../rated_tick_count.h"
#include "omap3530_mcbsp_frame.h"
#include "omap3530_mcbsp_link.h"
#include "omap3530_mcbsp_regs.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class StateReader;
class StateWriter;
class Omap3530McBspClockInputs;

class Omap3530McBspTimingListener {
public:
    virtual void OnTimingWillChange(uint64_t now) = 0;
    virtual void OnTimingRestarted(uint64_t now)  = 0;
    virtual void OnTimingRescaled(uint64_t now)   = 0;

protected:
    ~Omap3530McBspTimingListener() = default;
};

template <uint32_t kPort>
class Omap3530McBspPortClock : public Service, public Omap3530McBspPeerListener {
public:
    using Service::Service;
    using Dir = Omap3530McBspDir;

    enum class Change : uint8_t { None, Rescaled, Restarted };
    struct Changes {
        Change tx = Change::None;
        Change rx = Change::None;
    };

    bool ShouldRegister() override;
    void OnReady() override;

    void    SetListener(Dir dir, Omap3530McBspTimingListener* listener);
    Changes Apply(const Omap3530McBspRegs& regs, uint64_t now);

    const Omap3530McBspFrame& Layout(Dir dir) const { return layout_[Idx(dir)]; }
    bool                      Running(Dir dir) const;
    bool                      FrameSyncGated(Dir dir) const { return d_[Idx(dir)].fs == Fs::Gated; }
    void                      RequireRunnable(Dir dir, uint64_t now) const;
    Omap3530McBspPeer*        Peer() const { return peer_; }

    uint64_t             FrameBitCycle(Dir dir, uint64_t n, uint64_t bit) const;
    uint64_t             SlotEndCycle(Dir dir, uint64_t take) const;
    uint64_t             FirstTakeEndingAfter(Dir dir, uint64_t lo, uint64_t hi, uint64_t now) const;
    uint64_t             FirstFrameAfter(Dir dir, uint64_t cycle) const;
    Omap3530McBspDxFrame FrameShape(Dir dir, uint64_t n) const;

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore(const Omap3530McBspRegs& regs);

    void OnPeerTimingWillChange(uint64_t now) override;
    void OnPeerRestarted(uint64_t now) override;
    void OnPeerRescaled(uint64_t now) override;

private:
    enum class Clk : uint8_t { None, Peer, Clkg };
    enum class Fs : uint8_t { None, Peer, Fsg, Gated, Resync };
    enum class SrgSource : uint8_t { None, Clks, Iclk, ClkxPin };

    struct Srg {
        bool                  clkg_on   = false;
        bool                  fsg_on    = false;
        uint64_t              fsg_first = 0;
        uint32_t              period    = 1;
        GuestCycleClock::Rate clkg_rate = GuestCycleClock::Rate{0u, 1u};
    };

    struct Derived {
        Clk      clk       = Clk::None;
        Fs       fs        = Fs::None;
        uint32_t edge      = 0;
        uint32_t fb        = 0;
        bool     fsx_shape = false;
        bool     fsxp      = false;
        uint32_t fwid      = 0;
    };

    static constexpr size_t Idx(Dir dir) { return static_cast<size_t>(dir); }
    static bool UsesClkg(const Derived& d);
    static bool SameGrid(const Derived& a, const Srg& sa, const Derived& b, const Srg& sb);

    bool                  Rebuild(Dir dir);
    SrgSource             SrgSourceSelected() const;
    GuestCycleClock::Rate SrgInput() const;
    GuestCycleClock::Rate ClkgRate() const;
    bool                  SrgFromIclk() const;
    GuestCycleClock::Rate ClkxPinRate() const;
    bool                  PeerDrives() const;
    bool                  DependsOnPeer(const Derived& d) const;
    void                  UpdateSrg(uint64_t now);
    void                  UpdateModes();
    void                  UpdateReceiveModes();
    Change                Classify(Dir dir, const Derived& before, const Srg& srg_before, bool layout_changed) const;
    Changes               Refresh(uint64_t now, bool tx_layout_changed, bool rx_layout_changed);
    Changes               PeerChanges(uint64_t now, Change change);
    void                  Notify(const Changes& changes, uint64_t now);
    void                  WillChange(uint64_t now);
    void                  OnInputs();
    void                  OnCpuRate();

    uint64_t ClockCycleOfTick(const Derived& d, uint64_t tick) const;
    uint64_t ClockTickAtOrAfter(const Derived& d, uint64_t cycle) const;
    uint64_t FrameTick0(const Derived& d, uint64_t n) const;
    uint64_t StartCycle(const Derived& d, uint64_t n) const;
    bool     AnchorResync(const Derived& d, uint64_t n, RatedTickCount& at) const;

    GuestCycleClock*                           clock_    = nullptr;
    Omap3530McBspClockInputs*                  inputs_   = nullptr;
    Omap3530McBspPeer*                         peer_     = nullptr;
    Omap3530McBspTimingListener*               listener_[2] = {nullptr, nullptr};

    Omap3530McBspRegs  regs_;
    Omap3530McBspFrame layout_[2];
    const char*        layout_error_[2] = {nullptr, nullptr};
    const char*        rx_source_error_ = nullptr;
    Derived            d_[2];
    Srg                srg_;
    RatedTickCount     clkg_;
    TickPosition       restored_clkg_;
};

using Omap3530McBsp1PortClock = Omap3530McBspPortClock<1>;
using Omap3530McBsp2PortClock = Omap3530McBspPortClock<2>;
using Omap3530McBsp3PortClock = Omap3530McBspPortClock<3>;
using Omap3530McBsp4PortClock = Omap3530McBspPortClock<4>;
using Omap3530McBsp5PortClock = Omap3530McBspPortClock<5>;
