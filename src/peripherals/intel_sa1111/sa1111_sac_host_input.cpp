#include "sa1111_sac_host_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include "../../host/audio_activity_widget.h"
#include "../../execution/guest_cycle_clock.h"

#include <cstring>

namespace cerfcore {

namespace {

constexpr uint16_t kChannels      = 2u;
constexpr uint16_t kBitsPerSample = 16u;
constexpr uint32_t kWordBytes     = 4u;
constexpr size_t   kMicFifoBytes  = 65536u;

}

bool Sa1111SacHostInput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111SacHostInput::OnReady() {
    mic_.Start("Sa1111SacCapture", kMicFifoBytes);
}

void Sa1111SacHostInput::OnShutdown() { mic_.Stop(); }

void Sa1111SacHostInput::SetRate(uint64_t fs_num, uint64_t fs_den) {
    rate_ = static_cast<uint32_t>(GuestCycleClock::Rate{fs_num, fs_den}.RoundedHz());
}

/* SA-1111 Developer's Manual Table 7-30 (printed 7-21): DTL 15:0 left, DTH 31:16 right. */
void Sa1111SacHostInput::Take(uint8_t* dst, uint32_t words) {
    if (rate_ == 0u) {
        std::memset(dst, 0, kWordBytes * words);
        return;
    }
    mic_.Request(rate_, kChannels, kBitsPerSample);
    if (mic_.Take(dst, kWordBytes * words) != 0u) emu_.Get<AudioActivityWidget>().MarkRx();
}

void Sa1111SacHostInput::Clear() { mic_.Clear(); }

REGISTER_SERVICE(Sa1111SacHostInput);

}
