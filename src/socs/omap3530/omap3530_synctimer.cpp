#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "omap3530_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../cycle_anchored_counter.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "omap3530_clocks.h"

#include <cstdint>

namespace {

/* OMAP3530 TRM SPRUF98Y Table 16-92 (printed p. 2662): base 0x4832 0000,
   4K bytes. Table 16-93: REG_32KSYNCNT_REV 0x0000 R, REG_32KSYNCNT_SYSCONFIG
   0x0004 R/W, REG_32KSYNCNT_CR 0x0010 R. */
constexpr uint32_t kSynctimerBasePa = 0x48320000u;
constexpr uint32_t kSynctimerSize   = 0x00001000u;

constexpr uint32_t kOffRev       = 0x00;
constexpr uint32_t kOffSysconfig = 0x04;
constexpr uint32_t kOffCr        = 0x10;

/* Table 16-98 (printed p. 2663): REG_32KSYNCNT_CR COUNTER_VALUE [31:0],
   reset 0x00000003. */
constexpr uint32_t kCounterResetValue = 0x00000003u;

class Omap3530Synctimer : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Omap3530;
    }

    /* §16.6.1 (printed p. 2660): the counter is reset only while the external
       asynchronous power-up reset sys_nrespwron is active. §4.5.2.2 (printed
       p. 258): a global warm reset does not apply to the 32-kHz sync timer. */
    void OnReady() override {
        clock_ = &emu_.Get<GuestCycleClock>();
        counter_.Anchor(clock_->Cycles(), kCounterResetValue);
        if (!counter_.SetRatio(clock_->CpuHz(), kOmap3530Clk32kHz)) RatioOverflow();
        clock_->RegisterRateListener([this] {
            if (!counter_.Rescale(clock_->Cycles(), clock_->CpuHz(), kOmap3530Clk32kHz)) {
                RatioOverflow();
            }
        });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) {
                counter_.Anchor(clock_->Cycles(), kCounterResetValue);
            }
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kSynctimerBasePa; }
    uint32_t MmioSize() const override { return kSynctimerSize; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    [[noreturn]] void RatioOverflow() const {
        emu_.Get<Fatal>().Die(
            "omap3530 synctimer: the %llu Hz counter clock against the %llu Hz core "
            "overflows the 64-bit scale",
            static_cast<unsigned long long>(kOmap3530Clk32kHz),
            static_cast<unsigned long long>(clock_->CpuHz()));
    }

    GuestCycleClock*           clock_ = nullptr;
    NarrowCycleAnchoredCounter counter_;
};

uint32_t Omap3530Synctimer::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    switch (off) {
    /* Table 16-94 (printed p. 2662): CID_REV [7:0] holds the counter revision
       number, whose reset value the manual gives as TI internal data. */
    case kOffRev:       return 0u;
    /* §16.6.1 (printed p. 2660): a free-running 32-bit upward counter that
       wraps back to 0 after 0xFFFF FFFF. */
    case kOffCr:        return counter_.CountAt(clock_->Cycles());
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Omap3530Synctimer::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    switch (off) {
    /* Table 16-93 (printed p. 2662): REV and CR are type R. §16.6.1.2 (printed
       p. 2660): "no write operation is supported (no error/no action on
       write)". */
    case kOffRev:       return;
    case kOffCr:        return;
    /* Table 16-96 (printed p. 2663): REG_32KSYNCNT_SYSCONFIG "is used for IDLE
       modes only"; its one field IDLEMODE [4:3] is "Power management REQ/ACK
       control". */
    case kOffSysconfig: return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Omap3530Synctimer::SaveState(StateWriter& w) {
    const uint64_t now = clock_->Cycles();
    w.Write<uint32_t>("counter", counter_.CountAt(now));
    w.Write<uint64_t>("clk32k_phase", counter_.PhaseAt(now));
    w.Write<uint64_t>("clk32k_phase_den", counter_.PhaseDenominator());
}

void Omap3530Synctimer::RestoreState(StateReader& r) {
    uint32_t counter = 0;
    uint64_t phase = 0, phase_den = 0;
    r.Read("counter", counter);
    r.Read("clk32k_phase", phase);
    r.Read("clk32k_phase_den", phase_den);
    if (!counter_.SetRatio(clock_->CpuHz(), kOmap3530Clk32kHz)) RatioOverflow();
    if (!counter_.AnchorAtPhase(clock_->Cycles(), counter, phase, phase_den)) {
        r.Reject("omap3530 synctimer: restored 32-kHz phase %llu/%llu is not a "
                 "fraction of one tick this build can place",
                 static_cast<unsigned long long>(phase),
                 static_cast<unsigned long long>(phase_den));
    }
}

}

REGISTER_SERVICE(Omap3530Synctimer);
