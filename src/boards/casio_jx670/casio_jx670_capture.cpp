#include "casio_jx670_capture.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../host/audio_activity_widget.h"
#include <cerfengine/mips/mips_mmu.h>
#include "casio_jx670_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

constexpr uint8_t  kNoCapturedSample = 0u;
constexpr size_t   kMicFifoBytes     = 65536u;
constexpr uint16_t kChannels         = 1u;
constexpr uint16_t kBitsPerSample    = 16u;

}

bool CasioJx670Capture::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioJx670;
}

void CasioJx670Capture::OnReady() {
    mem_      = &emu_.Get<EmulatedMemory>();
    activity_ = &emu_.Get<AudioActivityWidget>();
    mic_.Start("Em500Capture", kMicFifoBytes);
}

void CasioJx670Capture::OnShutdown() { mic_.Stop(); }

void CasioJx670Capture::Request(uint32_t rate_hz) {
    mic_.Request(rate_hz, kChannels, kBitsPerSample);
}

void CasioJx670Capture::Clear() { mic_.Clear(); }

void CasioJx670Capture::WriteBlock(uint32_t va, uint32_t length) {
    const uint32_t       slots = length / kFrameBytes;
    std::vector<uint8_t> pcm(static_cast<size_t>(slots) * 2u);
    const bool           heard = mic_.Take(pcm.data(), pcm.size()) != 0u;
    std::vector<uint8_t> block(length, kNoCapturedSample);
    for (uint32_t i = 0; i < slots; ++i) {
        block[i * kFrameBytes]      = pcm[i * 2u];
        block[i * kFrameBytes + 1u] = pcm[i * 2u + 1u];
    }
    mem_->CopyIn(cerfengine::MipsSeg::UnmappedPa(va), block.data(), length);
    if (heard) activity_->MarkRx();
}

REGISTER_SERVICE(CasioJx670Capture);

}
