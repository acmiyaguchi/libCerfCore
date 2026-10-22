#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include "usb_bot_host.h"
#include "usb_hex.h"

#include <algorithm>
#include <cstdint>

namespace cerfcore {

namespace {

/* USB MSC BOT 1.0 section 5.1 CBW (31 bytes) and 5.2 CSW (13 bytes): signatures 43425355h and
   53425355h, bmCBWFlags [7] Data-In, bCSWStatus 0 Command Passed. */
constexpr uint32_t kCbwLength    = 31u;
constexpr uint32_t kCswLength    = 13u;
constexpr uint32_t kCbwSignature = 0x43425355u;
constexpr uint32_t kCswSignature = 0x53425355u;
constexpr uint8_t  kFlagIn       = 0x80u;
constexpr uint8_t  kCbwCbOffset  = 15u;

/* SPC-3 sections 6.33 TEST UNIT READY 00h, 6.27 REQUEST SENSE 03h (18 bytes) and 6.4 INQUIRY 12h
   (36-byte standard data); SBC-2 sections 5.10 READ CAPACITY (10) 25h (8 bytes) and 5.6 READ (10) 28h. */
constexpr uint8_t  kTestUnitReady = 0x00u;
constexpr uint8_t  kRequestSense  = 0x03u;
constexpr uint8_t  kInquiry       = 0x12u;
constexpr uint8_t  kReadCapacity  = 0x25u;
constexpr uint8_t  kRead10        = 0x28u;
constexpr uint32_t kInquiryLength = 36u;
constexpr uint32_t kCapacityLength = 8u;
constexpr uint32_t kSenseLength   = 18u;
constexpr uint32_t kMaxTurRetries = 8000u;

constexpr uint32_t kLogFrom      = 446u;
constexpr uint32_t kLogTo        = 512u;

enum Command : uint32_t { kCmdTestUnitReady, kCmdInquiry, kCmdReadCapacity, kCmdRead, kCommands, kCmdRequestSense };

void Put32(uint8_t* p, uint32_t v) {
    for (uint32_t i = 0; i < 4u; ++i) p[i] = static_cast<uint8_t>(v >> (8u * i));
}

uint32_t Get32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t GetBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

}  /* namespace */

void UsbBotHost::Die(const char* what, uint32_t a, uint32_t b) const {
    emu_.Get<Fatal>().Die("UsbBotHost: %s (%08X %08X, command %u)", what, a, b, command_);
}

void UsbBotHost::Start(uint32_t in_token, uint32_t out_token) {
    in_token_  = in_token;
    out_token_ = out_token;
    command_ = kCmdTestUnitReady;
    retries_ = 0u;
    phase_   = kCbw;
    block_   = 0u;
}

bool UsbBotHost::Step(const Transact& port) {
    switch (phase_) {
        case kCbw:  SendCbw(port); break;
        case kData: ReadData(port); break;
        case kCsw:  ReadCsw(port); break;
        default:    break;
    }
    return phase_ != kOff && phase_ != kDone;
}

/* BOT 1.0 section 5.1: CBW fields at 0 signature, 4 tag, 8 data transfer length, 12 flags, 13 LUN,
   14 CB length, 15 CB. */
void UsbBotHost::SendCbw(const Transact& port) {
    uint8_t cbw[kCbwLength] = {};
    uint8_t* cb = cbw + kCbwCbOffset;
    uint8_t  cb_length = 6u;
    switch (command_) {
        case kCmdTestUnitReady:
            cb[0] = kTestUnitReady;
            length_ = 0u;
            break;
        case kCmdRequestSense:
            cb[0] = kRequestSense;
            cb[4] = static_cast<uint8_t>(kSenseLength);
            length_ = kSenseLength;
            break;
        case kCmdInquiry:
            cb[0] = kInquiry;
            cb[4] = static_cast<uint8_t>(kInquiryLength);
            length_ = kInquiryLength;
            break;
        case kCmdReadCapacity:
            cb[0] = kReadCapacity;
            cb_length = 10u;
            length_ = kCapacityLength;
            break;
        default:
            cb[0] = kRead10;
            cb[8] = 1u;
            cb_length = 10u;
            length_ = block_;
            break;
    }
    Put32(cbw, kCbwSignature);
    Put32(cbw + 4, tag_ + 1u);
    Put32(cbw + 8, length_);
    cbw[12] = length_ != 0u ? kFlagIn : 0u;
    cbw[14] = cb_length;
    Result t{};
    if (!port(out_token_, cbw, kCbwLength, t)) return;
    ++tag_;
    LOG(UsbOtg, "UsbBotHost: CBW tag %u CB %s\n", tag_, UsbHex(cb, cb_length).c_str());
    data_.clear();
    phase_ = length_ != 0u ? kData : kCsw;
}

/* BOT 1.0 section 6.7.2 Hi = Di: the data stage ends on the expected length or a short packet. */
void UsbBotHost::ReadData(const Transact& port) {
    std::vector<uint8_t> buf(length_ - data_.size());
    Result t{};
    if (!port(in_token_, buf.data(), static_cast<uint32_t>(buf.size()), t)) return;
    data_.insert(data_.end(), buf.begin(), buf.begin() + t.moved);
    if (t.short_packet || data_.size() == length_) phase_ = kCsw;
}

/* BOT 1.0 section 5.2: CSW fields at 0 signature, 4 tag, 8 data residue, 12 status. */
void UsbBotHost::ReadCsw(const Transact& port) {
    uint8_t csw[kCswLength] = {};
    Result t{};
    if (!port(in_token_, csw, kCswLength, t)) return;
    if (t.moved != kCswLength || Get32(csw) != kCswSignature) Die("CSW length/signature", t.moved, Get32(csw));
    if (Get32(csw + 4) != tag_) Die("CSW tag", Get32(csw + 4), tag_);
    LOG(UsbOtg, "UsbBotHost: CSW tag %u residue %u status %u, data %u bytes\n", tag_, Get32(csw + 8),
        csw[12], static_cast<uint32_t>(data_.size()));
    if (csw[12] != 0u) {
        if (command_ != kCmdTestUnitReady) Die("CSW status not Command Passed", csw[12], Get32(csw + 8));
        if (++retries_ > kMaxTurRetries) {
            LOG(UsbOtg, "UsbBotHost: TEST UNIT READY not ready after %u retries\n", retries_ - 1u);
            phase_ = kDone;
            return;
        }
        command_ = kCmdRequestSense;
        phase_   = kCbw;
        return;
    }
    Finish();
}

/* SPC-3 section 4.5.3 fixed format sense data: [2] bits 3:0 sense key, [12] additional sense code;
   section 6.4.2 standard INQUIRY data: [0] peripheral device type, [8..15] vendor, [16..31] product;
   SBC-2 section 5.10 READ CAPACITY (10) data: last LBA [0..3], block length [4..7], big-endian. */
void UsbBotHost::Finish() {
    switch (command_) {
        case kCmdRequestSense:
            if (data_.size() < kSenseLength) Die("REQUEST SENSE data length", static_cast<uint32_t>(data_.size()), 0u);
            if (retries_ == 1u) {
                LOG(UsbOtg, "UsbBotHost: REQUEST SENSE key %02X ASC %02X\n", data_[2] & 0x0Fu, data_[12]);
            }
            command_ = kCmdTestUnitReady;
            phase_   = kCbw;
            return;
        case kCmdInquiry:
            if (data_.size() < kInquiryLength) Die("INQUIRY data length", static_cast<uint32_t>(data_.size()), 0u);
            LOG(UsbOtg, "UsbBotHost: INQUIRY type %02X vendor '%.8s' product '%.16s'\n", data_[0],
                reinterpret_cast<const char*>(data_.data() + 8), reinterpret_cast<const char*>(data_.data() + 16));
            break;
        case kCmdReadCapacity:
            if (data_.size() != kCapacityLength) {
                Die("READ CAPACITY data length", static_cast<uint32_t>(data_.size()), 0u);
            }
            block_ = GetBe32(data_.data() + 4);
            LOG(UsbOtg, "UsbBotHost: READ CAPACITY last LBA %u block length %u\n", GetBe32(data_.data()),
                block_);
            if (block_ == 0u) Die("READ CAPACITY block length 0", GetBe32(data_.data()), block_);
            break;
        case kCmdRead:
            if (data_.size() != block_) Die("READ (10) data length", static_cast<uint32_t>(data_.size()), block_);
            if (data_.size() > kLogFrom) {
                const size_t end = std::min<size_t>(kLogTo, data_.size());
                LOG(UsbOtg, "UsbBotHost: LBA 0 [446..511] %s\n", UsbHex(data_.data() + kLogFrom, end - kLogFrom).c_str());
            }
            break;
        default:
            break;
    }
    ++command_;
    phase_ = command_ < kCommands ? kCbw : kDone;
}

void UsbBotHost::SaveState(StateWriter& w) {
    w.Write("bot_phase", phase_);
    w.Write("bot_command", command_);
    w.Write("bot_retries", retries_);
    w.Write("bot_in", in_token_);
    w.Write("bot_out", out_token_);
    w.Write("bot_tag", tag_);
    w.Write("bot_length", length_);
    w.Write("bot_block", block_);
    w.Write("bot_data_size", static_cast<uint32_t>(data_.size()));
    w.WriteBytes("bot_data", data_.data(), data_.size());
}

void UsbBotHost::RestoreState(StateReader& r) {
    uint32_t size = 0u;
    r.Read("bot_phase", phase_);
    r.Read("bot_command", command_);
    r.Read("bot_retries", retries_);
    r.Read("bot_in", in_token_);
    r.Read("bot_out", out_token_);
    r.Read("bot_tag", tag_);
    r.Read("bot_length", length_);
    r.Read("bot_block", block_);
    r.Read("bot_data_size", size);
    data_.resize(size);
    r.ReadBytes("bot_data", data_.data(), data_.size());
}

}
