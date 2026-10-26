#include "sd_card_base.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace {

enum State : uint32_t {
    kStateIdle  = 0u,
    kStateReady = 1u,
    kStateIdent = 2u,
    kStateStby  = 3u,
    kStateTran  = 4u,
    kStateData  = 5u,
    kStateRcv   = 6u,
};

constexpr uint32_t kR1ReadyForData = 1u << 8;
constexpr uint32_t kR1AppCmd       = 1u << 5;
constexpr uint32_t kR1StateShift   = 9u;

constexpr uint32_t kOcrBusy        = 0x80000000u;
constexpr uint32_t kOcrSectorAddr  = 0x40000000u;
constexpr uint32_t kOcrVoltage     = 0x00FF8000u;

void PutBits(uint32_t out[4], uint32_t start, uint32_t width, uint32_t value) {
    const uint32_t mask  = (width < 32u) ? ((1u << width) - 1u) : 0xFFFFFFFFu;
    const uint32_t off   = 3u - (start / 32u);
    const uint32_t shift = start & 31u;
    value &= mask;
    out[off] |= value << shift;
    if (width + shift > 32u) {
        out[off - 1u] |= value >> (32u - shift);
    }
}

/* JEDEC JESD84-A43 section 10.2 / SD Physical Layer Simplified Specification Section 3.8. */
uint8_t Crc7(const uint8_t* data, uint32_t length) {
    uint8_t crc = 0u;
    for (uint32_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            const uint8_t in = static_cast<uint8_t>((byte >> 7) & 1u);
            const uint8_t out = static_cast<uint8_t>((crc >> 6) & 1u);
            crc = static_cast<uint8_t>((crc << 1) & 0x7Fu);
            if (in ^ out) crc ^= 0x09u;
            byte = static_cast<uint8_t>(byte << 1);
        }
    }
    return crc;
}

void SealCrc7(uint32_t out[4]) {
    std::vector<uint8_t> bytes;
    for (uint32_t i = 0; i < 4u; ++i) cerf::be::Append32(bytes, out[i]);
    out[3] = (out[3] & 0xFFFFFF00u) |
             static_cast<uint32_t>((Crc7(bytes.data(), 15u) << 1) | 1u);
}

}  // namespace

uint32_t SdCardBase::StatusWord(uint32_t state) const {
    return (state << kR1StateShift) | kR1ReadyForData;
}

void SdCardBase::Reset() {
    state_       = kStateIdle;
    app_cmd_     = false;
    rca_         = 0u;
    block_len_   = 512u;
    multi_read_  = false;
    multi_write_ = false;
    next_sector_ = 0u;
    read_data_.clear();
}

/* SD Specifications Part 1: Physical Layer Simplified Specification Version 2.00;
   decompiled sdbus.dll c050716c, c05037cc, c0503bb0, c0505a98, c05047e0. */
MmcCommandResult SdCardBase::Command(uint8_t index, uint32_t argument, uint32_t response[4]) {
    const uint32_t before = state_;
    LOG(Boot, "SdCardBase: index %u arg %08X app_cmd %u state %u\n", index, argument, app_cmd_, before);

    if (app_cmd_) {
        app_cmd_ = false;
        switch (index) {
        /* Section 4.2.3 ACMD41 SD_SEND_OP_COND; sdbus.dll SDPollOcr c0503bb0. */
        case 41u:
            if (before != kStateIdle) break;
            state_ = kStateReady;
            response[0] = kOcrBusy | kOcrSectorAddr | kOcrVoltage;
            return MmcCommandResult::Short;

        /* Section 5.6 ACMD51 SEND_SCR; sdbus.dll SDAssignRcaReadCsd c0505a98. */
        case 51u:
            if (before != kStateTran) break;
            read_data_ = {0x02u, 0x35u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
            state_ = kStateData;
            response[0] = StatusWord(before) | kR1AppCmd;
            return MmcCommandResult::Short;

        /* Section 4.3.11 ACMD6 SET_BUS_WIDTH. */
        case 6u:
            if (before != kStateTran) break;
            response[0] = StatusWord(before) | kR1AppCmd;
            return MmcCommandResult::Short;

        /* Section 4.3.11 ACMD13 SD_STATUS. */
        case 13u:
            if (before != kStateTran) break;
            read_data_.assign(64u, 0u);
            state_ = kStateData;
            response[0] = StatusWord(before) | kR1AppCmd;
            return MmcCommandResult::Short;

        /* Section 4.3.12 ACMD42 SET_CLR_CARD_DETECT. */
        case 42u:
            if (before != kStateTran) break;
            response[0] = StatusWord(before) | kR1AppCmd;
            return MmcCommandResult::Short;

        /* Version 6.00 section 4.3.9.1 (2), (4): ACMD55 does not exist and an undefined ACMD is
           handled as the regular command. */
        default:
            break;
        }
    }

    switch (index) {
    /* Section 4.3.7 CMD0 GO_IDLE_STATE. */
    case 0u:
        Reset();
        return MmcCommandResult::NoResponse;

    /* Section 4.3.13 CMD8 SEND_IF_COND; sdbus.dll SDSendCmd8 c05037cc. */
    case 8u:
        if (before != kStateIdle) break;
        response[0] = argument;
        return MmcCommandResult::Short;

    /* Section 4.3.10 CMD55 APP_CMD; sdbus.dll SDCardSendAppCommand c05047e0; version 6.00
       section 4.3.9.1 (1) the response sets APP_CMD. */
    case 55u:
        app_cmd_ = true;
        response[0] = StatusWord(before) | kR1AppCmd;
        return MmcCommandResult::Short;

    /* Section 5.1 CMD2 ALL_SEND_CID; sdbus.dll SDAssignRcaReadCsd c0505a98. */
    case 2u: {
        if (before != kStateReady) break;
        state_ = kStateIdent;
        const uint8_t cid[16] = {
            0x03u, 'S', 'D', 'S', 'C', '3', '2', 'G',
            0x80u, 0x12u, 0x34u, 0x56u, 0x78u, 0x02u, 0x63u, 0x00u
        };
        for (uint32_t i = 0; i < 4u; ++i) response[i] = cerf::be::U32(cid, i * 4u);
        SealCrc7(response);
        return MmcCommandResult::Long;
    }

    /* Section 4.3.3 CMD3 SEND_RELATIVE_ADDR; sdbus.dll SDAssignRcaReadCsd c0505a98. */
    case 3u:
        if (before != kStateIdent) break;
        rca_ = 1u;
        state_ = kStateStby;
        response[0] = (static_cast<uint32_t>(rca_) << 16) | (StatusWord(before) & 0xFFFFu);
        return MmcCommandResult::Short;

    /* Section 5.3.3 CMD9 SEND_CSD (Version 2.0); sdbus.dll SDAssignRcaReadCsd c0505a98. */
    case 9u: {
        if (before != kStateStby) break;
        if ((argument >> 16) != rca_) return MmcCommandResult::NoResponse;
        response[0] = response[1] = response[2] = response[3] = 0u;
        PutBits(response, 126u, 2u,  1u);
        PutBits(response, 112u, 8u,  0x0Eu);
        PutBits(response, 104u, 8u,  0x00u);
        PutBits(response,  96u, 8u,  0x32u);
        PutBits(response,  84u, 12u, 0x5B5u);
        PutBits(response,  80u, 4u,  9u);
        const uint32_t c_size = (SectorCount() >= 1024u) ? (SectorCount() / 1024u - 1u) : 0u;
        PutBits(response,  48u, 22u, std::min(c_size, 0x3FFFFFu));
        PutBits(response,  46u, 1u,  1u);
        PutBits(response,  39u, 7u,  0x7Fu);
        PutBits(response,  26u, 3u,  2u);
        PutBits(response,  22u, 4u,  9u);
        SealCrc7(response);
        return MmcCommandResult::Long;
    }

    /* Section 4.3.9 CMD13 SEND_STATUS. */
    case 13u:
        if ((argument >> 16) != rca_) return MmcCommandResult::NoResponse;
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;

    /* Section 4.3.3 CMD7 SELECT/DESELECT_CARD; sdbus.dll SDAssignRcaReadCsd c0505a98. */
    case 7u:
        if ((argument >> 16) == rca_) {
            if (before != kStateStby && before != kStateTran) break;
            state_ = kStateTran;
            response[0] = StatusWord(before);
            return MmcCommandResult::Short;
        }
        state_ = kStateStby;
        return MmcCommandResult::NoResponse;

    /* Version 6.00 section 4.3.10 CMD6 SWITCH_FUNC, Table 4-13: function 0 of every group, the
       one function TRAN_SPEED 25 MHz in CMD9 allows; Tables 4-15 to 4-17 status codes; section
       4.2.3.1 XPC=0 limit of 100 mA. */
    case 6u: {
        if (before != kStateTran) break;
        read_data_.assign(64u, 0u);
        read_data_[1] = 100u;
        for (uint32_t group = 0; group < 6u; ++group) {
            read_data_[2u + group * 2u] = 0x80u;
            read_data_[3u + group * 2u] = 0x01u;
        }
        for (uint32_t group = 0; group < 6u; ++group) {
            const uint32_t arg  = (argument >> (group * 4u)) & 0xFu;
            const uint8_t  code = (arg == 0u || arg == 0xFu) ? 0u : 0xFu;
            read_data_[16u - group / 2u] |= static_cast<uint8_t>(code << ((group & 1u) * 4u));
        }
        state_ = kStateData;
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;
    }

    /* Section 4.3.11 CMD16 SET_BLOCKLEN. */
    case 16u:
        if (before != kStateTran) break;
        block_len_ = argument;
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;

    /* Section 4.3.11 CMD17 READ_SINGLE_BLOCK. */
    case 17u:
        if (before != kStateTran) break;
        read_data_.resize(512u);
        ReadBlock(argument, read_data_.data());
        next_sector_ = argument + 1u;
        multi_read_  = false;
        state_       = kStateData;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    /* Section 4.3.11 CMD18 READ_MULTIPLE_BLOCK. */
    case 18u:
        if (before != kStateTran) break;
        read_data_.resize(512u);
        ReadBlock(argument, read_data_.data());
        next_sector_ = argument + 1u;
        multi_read_  = true;
        state_       = kStateData;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    /* Section 4.3.11 CMD24 WRITE_BLOCK. */
    case 24u:
        if (before != kStateTran) break;
        next_sector_ = argument;
        multi_write_ = false;
        state_       = kStateRcv;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    /* Section 4.3.11 CMD25 WRITE_MULTIPLE_BLOCK. */
    case 25u:
        if (before != kStateTran) break;
        next_sector_ = argument;
        multi_write_ = true;
        state_       = kStateRcv;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    /* Section 4.3.11 CMD12 STOP_TRANSMISSION. */
    case 12u:
        multi_read_  = false;
        multi_write_ = false;
        read_data_.clear();
        state_       = kStateTran;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    default:
        break;
    }

    return MmcCommandResult::NoResponse;
}

void SdCardBase::NextBlock() {
    if (multi_read_) {
        read_data_.resize(512u);
        ReadBlock(next_sector_++, read_data_.data());
    } else {
        read_data_.clear();
        state_ = kStateTran;
    }
}

void SdCardBase::EndDataPhase() {
    read_data_.clear();
    if (!multi_read_ && !multi_write_) {
        state_ = kStateTran;
    }
}

void SdCardBase::ReceiveBlock(const uint8_t* data, uint32_t) {
    WriteBlock(next_sector_++, data);
    if (!multi_write_) {
        state_ = kStateTran;
    }
}

void SdCardBase::SaveState(StateWriter& w) {
    w.Write("card_state", state_);
    w.Write("app_cmd", static_cast<uint32_t>(app_cmd_ ? 1u : 0u));
    w.Write("rca", static_cast<uint32_t>(rca_));
    w.Write("block_len", block_len_);
    w.Write("multi_read", static_cast<uint32_t>(multi_read_ ? 1u : 0u));
    w.Write("multi_write", static_cast<uint32_t>(multi_write_ ? 1u : 0u));
    w.Write("next_sector", next_sector_);
}

void SdCardBase::RestoreState(StateReader& r) {
    uint32_t app_cmd = 0u, rca = 0u, multi_read = 0u, multi_write = 0u;
    r.Read("card_state", state_);
    r.Read("app_cmd", app_cmd);
    r.Read("rca", rca);
    r.Read("block_len", block_len_);
    r.Read("multi_read", multi_read);
    r.Read("multi_write", multi_write);
    r.Read("next_sector", next_sector_);
    app_cmd_     = (app_cmd != 0u);
    rca_         = static_cast<uint16_t>(rca);
    multi_read_  = (multi_read != 0u);
    multi_write_ = (multi_write != 0u);
}

}
