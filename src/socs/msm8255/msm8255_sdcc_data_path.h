#pragma once

#include "msm8255_clock_reset.h"
#include "msm8255_crci_bus.h"
#include "msm8255_sdcc_regs.h"
#include "msm8255_sdcc_restored_register.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../peripherals/mmc/mmc_card.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>
#include <vector>

namespace cerf_msm8255_sdcc_detail {

template <uint32_t kBase, uint32_t kResetClock, uint32_t kCrci>
class Msm8255SdccDataPath : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { ResetState(); });
        emu_.Get<Msm8255ClockReset>().RegisterListener(kResetClock, [this] {
            RequireNoTransferInFlight("a clkregim clock reset");
            ResetState();
        });
        emu_.Get<Msm8255CrciBus>().DeclareFifo(kCrci, kBase + kFifo,
                                              kFifoBytes);
    }

    void WriteDataTimer(uint32_t value) { data_timer_ = value; }

    void WriteDataLength(uint32_t value) { data_length_ = value; }

    void StartDataPhase(uint32_t value, MmcCard* card) {
        RequireNoTransferInFlight("a data control write");
        data_ctrl_ = value;
        read_data_.clear();
        read_pos_ = 0u;
        send_block_.clear();
        if ((value & kDataCtrlEnable) == 0u) {
            DriveCrci();
            return;
        }
        const bool to_host = (value & kDataCtrlDirection) != 0u;
        if ((value & kDataCtrlDmaEnable) == 0u) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: data control 0x%08X starts a %s data "
                "phase without DMA, and the %s FIFO status bits are not "
                "modeled", kBase, value, to_host ? "card-to-host" : "host-to-card",
                to_host ? "receive" : "transmit");
        }
        if (card == nullptr) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: data control 0x%08X starts a data phase "
                "with no card in the slot", kBase, value);
        }
        const uint32_t block  = BlockBytes(value);
        const uint32_t length = data_length_;
        if (length > kDataLengthMax) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: data length 0x%08X sets bits past the "
                "%u-bit length field", kBase, length, kDataLengthBits);
        }
        if (block == 0u || length == 0u || length % block != 0u) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: data control 0x%08X carries a %u byte "
                "block over a %u byte transfer that is not a whole number of "
                "blocks, which is not modeled", kBase, value, block, length);
        }
        if (!to_host && block % kFifoWordBytes != 0u) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: data control 0x%08X sends %u byte blocks, "
                "which are not whole FIFO words, and that is not modeled",
                kBase, value, block);
        }
        data_count_ = length;
        DriveCrci();
    }

    void BindDataPhase(MmcCard& card) {
        const std::vector<uint8_t>& staged = card.ReadData();
        const bool armed = (data_ctrl_ & kDataCtrlEnable) != 0u;
        if (staged.empty()) {
            if (armed && !Sending() && read_data_.empty()) {
                emu_.Get<Fatal>().Die(
                    "Peripheral at 0x%08X: data control 0x%08X arms a data "
                    "phase the card answered with no data, and a data "
                    "timeout is not modeled", kBase, data_ctrl_);
            }
            return;
        }
        if (!armed || Sending() || data_count_ == 0u ||
            read_pos_ < read_data_.size()) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: the card answered with %u bytes while "
                "data control 0x%08X has %u bytes left to receive and %u "
                "bytes undrained", kBase, static_cast<unsigned>(staged.size()),
                data_ctrl_, data_count_,
                static_cast<unsigned>(read_data_.size() - read_pos_));
        }
        if (staged.size() != BlockBytes(data_ctrl_)) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: data control carries %u byte blocks and "
                "the card answered with a %u byte block", kBase,
                BlockBytes(data_ctrl_), static_cast<unsigned>(staged.size()));
        }
        read_data_ = staged;
        read_pos_  = 0u;
        DriveCrci();
    }

    template <typename CardAccess>
    uint32_t ReadFifo(uint32_t& events, CardAccess&& card_for_slot) {
        events = 0u;
        if (read_pos_ + kFifoWordBytes > read_data_.size()) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: fifo read at byte %u passes the %u bytes "
                "of the data phase in progress",
                kBase, read_pos_, static_cast<unsigned>(read_data_.size()));
        }
        const uint32_t v = cerf::le::U32(read_data_.data(), read_pos_);
        read_pos_ += kFifoWordBytes;
        if (read_pos_ != read_data_.size()) return v;

        MmcCard* card = card_for_slot();
        data_count_ -= read_pos_;
        if (data_count_ != 0u) {
            card->NextBlock();
            const std::vector<uint8_t>& next = card->ReadData();
            if (next.size() != read_data_.size()) {
                emu_.Get<Fatal>().Die(
                    "Peripheral at 0x%08X: the card answered the next block "
                    "with %u bytes where the transfer carries %u byte blocks",
                    kBase, static_cast<unsigned>(next.size()),
                    static_cast<unsigned>(read_data_.size()));
            }
            read_data_ = next;
            read_pos_  = 0u;
            events     = kStatusDataBlockEnd;
            return v;
        }
        card->EndDataPhase();
        DriveCrci();
        events = kStatusDataBlockEnd | kStatusDataEnd;
        return v;
    }

    template <typename CardAccess>
    uint32_t WriteFifo(uint32_t value, CardAccess&& card_for_slot) {
        if (!Sending() || data_count_ == 0u) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: fifo write of 0x%08X while no "
                "host-to-card data phase has bytes left to send", kBase, value);
        }
        cerf::le::Append32(send_block_, value);
        const uint32_t block = BlockBytes(data_ctrl_);
        if (send_block_.size() < block) return 0u;
        card_for_slot()->ReceiveBlock(send_block_.data(), block);
        send_block_.clear();
        data_count_ -= block;
        if (data_count_ != 0u) return kStatusDataBlockEnd;
        DriveCrci();
        return kStatusDataBlockEnd | kStatusDataEnd;
    }

    void SaveState(StateWriter& w) {
        w.Write<uint32_t>("data_timer", data_timer_);
        w.Write<uint32_t>("data_length", data_length_);
        w.Write<uint32_t>("data_control", data_ctrl_);
        w.Write<uint32_t>("read_pos", read_pos_);
        w.Write<uint32_t>("read_data_count", static_cast<uint32_t>(read_data_.size()));
        for (uint8_t b : read_data_) w.Write<uint8_t>("read_data", b);
        w.Write<uint32_t>("data_count", data_count_);
        w.Write<uint32_t>("send_block_count", static_cast<uint32_t>(send_block_.size()));
        for (uint8_t b : send_block_) w.Write<uint8_t>("send_block", b);
    }

    void RestoreState(StateReader& r, MmcCard* card) {
        uint32_t pos     = 0u;
        uint32_t staged  = 0u;
        uint32_t count   = 0u;
        uint32_t sending = 0u;
        r.Read("data_timer", data_timer_);
        r.Read("data_length", data_length_);
        data_ctrl_ = ReadRestoredRegister(r, "data_control", kDataCtrlModelled,
                                          kBase, kDataCtrl);
        r.Read("read_pos", pos);
        r.Read("read_data_count", staged);
        RequireWithinBlock(r, "read_data_count", staged);
        read_data_.resize(staged);
        for (uint32_t i = 0; i < staged; ++i) r.Read("read_data", read_data_[i]);
        r.Read("data_count", count);
        r.Read("send_block_count", sending);
        RequireWithinBlock(r, "send_block_count", sending);
        send_block_.resize(sending);
        for (uint32_t i = 0; i < sending; ++i) r.Read("send_block", send_block_[i]);
        RequireReachableDataPath(r, pos, staged, count, sending, card);
        read_pos_   = pos;
        data_count_ = count;
    }

    void PostRestore() { DriveCrci(); }

private:
    static constexpr uint32_t kFifoWordBytes = 4u;

    static uint32_t BlockBytes(uint32_t ctrl) {
        return (ctrl & kDataCtrlBlockSize) >> kDataCtrlBlockSizeShift;
    }

    bool Sending() const {
        return (data_ctrl_ & (kDataCtrlEnable | kDataCtrlDirection)) ==
               kDataCtrlEnable;
    }

    void RequireNoTransferInFlight(const char* cause) {
        if (read_pos_ < read_data_.size()) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: %s ends a transfer with %u of %u bytes "
                "undrained, and ending a transfer early is not modeled",
                kBase, cause, read_pos_,
                static_cast<unsigned>(read_data_.size()));
        }
        if (Sending() && data_count_ != 0u) {
            emu_.Get<Fatal>().Die(
                "Peripheral at 0x%08X: %s ends a host-to-card transfer with %u "
                "bytes still to send, and ending a transfer early is not "
                "modeled", kBase, cause, data_count_);
        }
    }

    void DriveCrci() {
        auto& lines = emu_.Get<Msm8255CrciBus>();
        if (read_pos_ < read_data_.size() || (Sending() && data_count_ != 0u)) {
            lines.Assert(kCrci);
        } else {
            lines.Deassert(kCrci);
        }
    }

    void RequireWithinBlock(StateReader& r, const char* tag, uint32_t bytes) {
        if (bytes > BlockBytes(data_ctrl_)) {
            r.Reject(
                "Peripheral at 0x%08X: restored %s %u exceeds the %u byte block "
                "of data control 0x%08X", kBase, tag, bytes,
                BlockBytes(data_ctrl_), data_ctrl_);
        }
    }

    void RequireReachableDataPath(StateReader& r, uint32_t pos, uint32_t staged,
                                  uint32_t count, uint32_t sending,
                                  const MmcCard* card) {
        const uint32_t ctrl   = data_ctrl_;
        const uint32_t block  = BlockBytes(ctrl);
        const bool     armed  = (ctrl & kDataCtrlEnable) != 0u;
        const bool     dma    = armed && (ctrl & kDataCtrlDmaEnable) != 0u &&
                                card != nullptr && block != 0u;
        const bool     to_host   = (ctrl & kDataCtrlDirection) != 0u;
        const bool     receiving = dma && to_host;
        const bool     sends     = dma && !to_host && block % kFifoWordBytes == 0u;
        const bool     whole     = block != 0u && count % block == 0u;
        bool reachable = count <= kDataLengthMax;
        if (staged != 0u) {
            reachable = reachable && sending == 0u && receiving &&
                        (pos & 3u) == 0u && pos <= staged && staged == block &&
                        whole && (pos < staged ? count >= block : count == 0u);
        } else if (sending != 0u) {
            reachable = reachable && pos == 0u && sends && whole &&
                        sending % kFifoWordBytes == 0u && sending < block &&
                        count >= block;
        } else {
            reachable = reachable && pos == 0u &&
                        (!armed || (receiving && whole && count != 0u) ||
                         (sends && whole));
        }
        if (!reachable) {
            r.Reject(
                "Peripheral at 0x%08X: restored fifo cursor %u over %u staged "
                "bytes and %u bytes of a block being sent, with %u bytes left "
                "under data control 0x%08X, is not a state this data path "
                "reaches", kBase, pos, staged, sending, count, ctrl);
        }
    }

    void ResetState() {
        data_timer_  = kUngroundedPowerOn;
        data_length_ = kUngroundedPowerOn;
        data_ctrl_   = kUngroundedPowerOn;
        read_data_.clear();
        read_pos_ = 0u;
        send_block_.clear();
        data_count_ = kUngroundedPowerOn;
        DriveCrci();
    }

    uint32_t             data_timer_  = kUngroundedPowerOn;
    uint32_t             data_length_ = kUngroundedPowerOn;
    uint32_t             data_ctrl_   = kUngroundedPowerOn;
    std::vector<uint8_t> read_data_;
    uint32_t             read_pos_   = 0u;
    std::vector<uint8_t> send_block_;
    uint32_t             data_count_ = kUngroundedPowerOn;
};

}  // namespace cerf_msm8255_sdcc_detail
