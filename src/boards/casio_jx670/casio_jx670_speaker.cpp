#include "casio_jx670_speaker.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../host/audio_activity_widget.h"
#include <cerfengine/mips/mips_mmu.h>
#include "casio_jx670_family_id.h"

#include <vector>

namespace cerfcore {

bool CasioJx670Speaker::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioJx670;
}

void CasioJx670Speaker::OnReady() {
    mem_      = &emu_.Get<EmulatedMemory>();
    activity_ = &emu_.Get<AudioActivityWidget>();
    paced_.Start("Em500Audio", 0, 0, 0, true);
    activity_->NotePresent();
}

void CasioJx670Speaker::OnShutdown() { paced_.Stop(); }

void CasioJx670Speaker::SetFormat(uint32_t hz, uint16_t channels, uint16_t bits) {
    paced_.SetFormat(hz, channels, bits);
}

void CasioJx670Speaker::Begin() { paced_.BeginAudioOut(); }

void CasioJx670Speaker::Finish() { paced_.FinishAudioOut(); }

void CasioJx670Speaker::Abort() { paced_.StopAudioOut(); }

void CasioJx670Speaker::QueueBlock(uint32_t va, uint32_t length) {
    std::vector<uint8_t> block(length);
    /* VR4102 UM ch.5 p131 "(3) kseg1": references are not mapped through TLB and the
       physical address is the virtual address minus 0xA0000000. */
    mem_->CopyOut(cerfengine::MipsSeg::UnmappedPa(va), block.data(), length);
    activity_->MarkTx();
    paced_.QueueOutputInHostBlocks(block.data(), length);
}

REGISTER_SERVICE(CasioJx670Speaker);

}
