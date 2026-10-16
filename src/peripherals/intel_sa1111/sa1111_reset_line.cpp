#include "sa1111_reset_line.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

bool Sa1111ResetLine::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111ResetLine::OnReady() {
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind kind) {
        power_on_pending_ = kind == ResetLineKind::Rtc;
    });
    reset.RegisterResetReleaseListener([this] {
        if (!power_on_pending_) return;
        power_on_pending_ = false;
        Assert();
    });
}

void Sa1111ResetLine::RegisterListener(std::function<void(bool held)> fn) {
    listeners_.push_back(std::move(fn));
}

void Sa1111ResetLine::Assert() {
    held_.store(true, std::memory_order_release);
    for (auto& fn : listeners_) fn(true);
}

/* SA-1111 Developer's Manual §2.3: "When nRESET is asserted, all on-chip activity halts;
   when nRESET is released, the SA-1111 goes to doze mode". */
void Sa1111ResetLine::Drive(Pin level, bool resync) {
    const bool asserted = level == Pin::Low && pin_ != Pin::Low;
    pin_ = level;
    if (!resync && asserted) Assert();
}

/* SA-1111 Developer's Manual Figure 2-2 (printed 2-5): nBRES stays asserted after nRESET
   rises and rises after RCLKEn. */
void Sa1111ResetLine::OnSkcrWrite(bool rclk_enabled) {
    if (rclk_enabled && pin_ == Pin::High && Held()) Release();
}

void Sa1111ResetLine::Release() {
    held_.store(false, std::memory_order_release);
    for (auto& fn : listeners_) fn(false);
}

void Sa1111ResetLine::SaveState(StateWriter& w) {
    w.Write<uint8_t>("nreset_held", Held() ? 1u : 0u);
}

void Sa1111ResetLine::RestoreState(StateReader& r) {
    uint8_t held = 0u;
    r.Read("nreset_held", held);
    held_.store(held != 0u, std::memory_order_release);
}

REGISTER_SERVICE(Sa1111ResetLine);

}
