#include "sa1111_sac_dma.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/cpu/emulated_memory.h>
#include "../../host/audio_activity_widget.h"
#include "sa1111_sac_host_output.h"
#include "sa1111_sac_request_lines.h"
#include "sa1111_sac_tx_stream.h"

#include <vector>

namespace cerfcore {

namespace {

const Sa1111SacDmaChannel::Names kTxNames{
    {"sadtcs", "sadtsa", "sadtca", "sadtsb", "sadtcb"},
    "tx_running", "tx_buffer_b", "tx_words_left", "TX", "SADTCS",
    Sa1111SacRequestLines::kSrcDoneA, Sa1111SacRequestLines::kSrcDoneB};

}

Sa1111SacDma::Sa1111SacDma(CerfEmulator& emu) : Sa1111SacDmaChannel(emu, kTxNames) {}

bool Sa1111SacDma::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void Sa1111SacDma::OnChannelReady() {
    stream_ = &emu_.Get<Sa1111SacTxStream>();
    host_   = &emu_.Get<Sa1111SacHostOutput>();
}

uint32_t Sa1111SacDma::WordsRequested() const {
    return running_ && Fifo().enabled ? words_left_ : 0u;
}

bool Sa1111SacDma::NextRequestCycle(uint64_t& cycle) const {
    const uint32_t words = WordsRequested();
    return words != 0u && stream_->NextRequestCycle(Threshold(), words, cycle);
}

uint32_t Sa1111SacDma::Threshold() const {
    return Fifo().threshold_level;
}

void Sa1111SacDma::FillTo(uint64_t now) {
    const uint64_t before = stream_->Requests();
    stream_->Fill(now, Threshold(), words_left_);
    if (stream_->Requests() != before) end_cycle_ = stream_->LandedCycle();
}

void Sa1111SacDma::SettleTo(uint64_t now) {
    stream_->Settle(now);
}

bool Sa1111SacDma::PlannedEnd(uint64_t& cycle) const {
    return stream_->DoneCycle(Threshold(), words_left_, cycle);
}

/* SA-1111 Developer's Manual Table 7-30 (printed 7-21): DTL 15:0 left, DTH 31:16 right. */
void Sa1111SacDma::OnBlockStart(uint64_t at, bool buffer_b) {
    const uint32_t bytes = ctl_.Bytes(buffer_b);
    host_->Begin();
    if (bytes != 0u) {
        auto&                mem  = emu_.Get<EmulatedMemory>();
        const uint32_t       addr = ctl_.Address(buffer_b);
        std::vector<uint8_t> block(bytes);
        for (uint32_t i = mem.TryCopyOut(addr, block.data(), bytes); i < bytes; ++i) {
            block[i] = mem.ReadByte(addr + i);
        }
        host_->Queue(block.data(), bytes);
    }
    stream_->StartBlock(at);
}

void Sa1111SacDma::OnBlockDone() {
    emu_.Get<AudioActivityWidget>().MarkTx();
}

/* SA-1111 Developer's Manual §7.2.2 (printed 7-5): the DMA can "loop continuously on a block-data
   transfer" with "Enable Interface Loop Back Function"; "The DMA Done interrupt still asserts". */
bool Sa1111SacDma::KeepStarted(bool buffer_b) const {
    return Fifo().loop_back && ctl_.Bytes(buffer_b) != 0u;
}

void Sa1111SacDma::OnStopped() {
    host_->End();
}

void Sa1111SacDma::OnReset(uint64_t) {
    host_->End();
}

Sa1111Sbi::BusGrant Sa1111SacDma::CurrentGrant() const {
    return stream_->Grant();
}

void Sa1111SacDma::ApplyGrant(uint64_t now, Sa1111Sbi::BusGrant grant) {
    stream_->SetGrant(now, grant);
}

void Sa1111SacDma::SaveExtra(StateWriter&) const {}

void Sa1111SacDma::RestoreExtra(StateReader&, uint64_t) {
    host_->Drop();
}

void Sa1111SacDma::OnRestored() {
    if (running_) host_->Begin();
}

REGISTER_SERVICE(Sa1111SacDma);

}
