#include "sa1111_sac_frame_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/state/state_stream.h>

bool Sa1111SacFrameClock::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

bool Sa1111SacFrameClock::Rescale(uint64_t now, uint64_t cpu_hz, GuestCycleClock::Rate fs) {
    if (!clocked_) {
        if (!frames_.SetRatio(cpu_hz * fs.den, fs.num)) return false;
        frames_.Anchor(now, 0u);
        base_    = frozen_;
        clocked_ = true;
    } else if (cpu_hz != cpu_hz_ || fs.num != fs_num_ || fs.den != fs_den_) {
        const uint64_t pos = Position(now);
        if (!frames_.Rescale(now, cpu_hz * fs.den, fs.num)) return false;
        base_ = pos - frames_.TicksSince(now);
    }
    cpu_hz_ = cpu_hz;
    fs_num_ = fs.num;
    fs_den_ = fs.den;
    return true;
}

/* SA-1111 Developer's Manual §7.3.2.2: SYS_CLK "is always 256 times the audio sampling
   frequency"; §5.2.1 I2SCLKEn gates it. */
void Sa1111SacFrameClock::Stop(uint64_t now) {
    if (!clocked_) return;
    frozen_  = Position(now);
    clocked_ = false;
}

uint64_t Sa1111SacFrameClock::Position(uint64_t now) const {
    return clocked_ ? base_ + frames_.TicksSince(now) : frozen_;
}

void Sa1111SacFrameClock::Save(StateWriter& w, uint64_t now) const {
    w.Write<uint8_t>("tx_frame_clocked", clocked_ ? 1u : 0u);
    w.Write<uint64_t>("tx_frame_cpu_hz", clocked_ ? cpu_hz_ : 0u);
    w.Write<uint64_t>("tx_frame_fs_num", clocked_ ? fs_num_ : 0u);
    w.Write<uint64_t>("tx_frame_fs_den", clocked_ ? fs_den_ : 1u);
    w.Write<uint64_t>("tx_frame_phase", clocked_ ? frames_.PhaseAt(now) : 0u);
    w.Write<uint64_t>("tx_frame_phase_den", clocked_ ? frames_.PhaseDenominator() : 1u);
}

void Sa1111SacFrameClock::Restore(StateReader& r, uint64_t now) {
    uint8_t  clocked = 0u;
    uint64_t cpu_hz = 0u, fs_num = 0u, fs_den = 1u, phase = 0u, phase_den = 0u;
    r.Read("tx_frame_clocked", clocked);
    r.Read("tx_frame_cpu_hz", cpu_hz);
    r.Read("tx_frame_fs_num", fs_num);
    r.Read("tx_frame_fs_den", fs_den);
    r.Read("tx_frame_phase", phase);
    r.Read("tx_frame_phase_den", phase_den);
    clocked_ = clocked != 0u;
    frozen_  = 0u;
    if (clocked_) {
        frames_.SetRatio(cpu_hz * fs_den, fs_num);
        frames_.AnchorAtPhase(now, 0u, phase, phase_den);
        cpu_hz_ = cpu_hz;
        fs_num_ = fs_num;
        fs_den_ = fs_den;
    }
    base_ = 0u - frames_.TicksSince(now);
}

REGISTER_SERVICE(Sa1111SacFrameClock);
