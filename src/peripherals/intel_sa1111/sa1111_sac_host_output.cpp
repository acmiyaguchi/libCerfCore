#include "sa1111_sac_host_output.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include "../../execution/guest_cycle_clock.h"

namespace cerfcore {

namespace {

constexpr uint16_t kChannels       = 2u;
constexpr uint16_t kBitsPerSample  = 16u;
constexpr uint32_t kHostIdleRateHz = 44100u;

}

bool Sa1111SacHostOutput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111SacHostOutput::OnReady() {
    out_.Start("Sa1111Sac", kHostIdleRateHz, kChannels, kBitsPerSample, true);
    rate_ = kHostIdleRateHz;
}

void Sa1111SacHostOutput::OnShutdown() { out_.Stop(); }

void Sa1111SacHostOutput::SetRate(uint64_t fs_num, uint64_t fs_den) {
    const uint32_t rate = static_cast<uint32_t>(GuestCycleClock::Rate{fs_num, fs_den}.RoundedHz());
    if (rate == rate_) return;
    rate_ = rate;
    out_.SetFormat(rate, kChannels, kBitsPerSample);
}

void Sa1111SacHostOutput::Begin() {
    if (active_) return;
    out_.BeginAudioOut();
    active_ = true;
}

void Sa1111SacHostOutput::End() {
    if (active_) out_.FinishAudioOut();
    active_ = false;
}

void Sa1111SacHostOutput::Queue(const uint8_t* data, uint32_t bytes) {
    out_.QueueOutput(data, bytes);
}

void Sa1111SacHostOutput::Drop() {
    out_.StopAudioOut();
    active_ = false;
    rate_   = 0u;
}

REGISTER_SERVICE(Sa1111SacHostOutput);

}
