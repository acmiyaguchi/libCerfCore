#include "emmc_card_base.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

using namespace cerf_mmc;

namespace {

constexpr uint32_t kExtCsdBytes    = 512u;
constexpr uint32_t kExtCsdSecCount = 212u;
constexpr uint32_t kSecCountBytes  = 4u;

bool IsCardOwnedExtCsdByte(uint32_t offset) {
    return offset >= kExtCsdBytes || offset == kExtCsdHsTiming ||
           offset == kExtCsdUserWp || offset == kExtCsdBusWidth ||
           offset == kExtCsdErasedMemCont || offset == kExtCsdEraseGroupDef ||
           (offset >= kExtCsdSecCount && offset < kExtCsdSecCount + kSecCountBytes);
}

}  // namespace

void EmmcCardBase::OnReady() {
    power_on_wp_.assign(WpGroupCount(), 0u);
    emu_.Get<GuestCpuReset>().RegisterResetListener(
        [this](ResetLineKind) { Reset(); });
}

MmcCommandResult EmmcCardBase::Command(uint8_t index, uint32_t argument,
                                       uint32_t response[4]) {
    const MmcState before  = state_;
    const uint16_t arg_rca = static_cast<uint16_t>(argument >> 16);
    const uint32_t preset  = block_count_;
    block_count_ = 0u;

    read_data_.clear();

    switch (index) {
    case kCmdGoIdleState:
        if (argument != kGoIdleArgument) break;
        multi_read_  = false;
        multi_write_ = false;
        blocks_left_ = 0u;
        state_     = MmcState::Idle;
        rca_       = 0u;
        hs_timing_ = 0u;
        user_wp_   = 0u;
        return MmcCommandResult::NoResponse;

    case kCmdIoRwDirect:
        return MmcCommandResult::NoResponse;

    case kCmdSleepAwake:
    case kCmdAppCmd:
        if (before != MmcState::Idle && before != MmcState::Ready &&
            before != MmcState::Ident) {
            break;
        }
        return MmcCommandResult::NoResponse;

    case kCmdSendOpCond:
        if (before != MmcState::Idle) break;
        state_      = MmcState::Ready;
        response[0] =
            kOcrBusy | kOcrSectorAddr | kOcrVoltage | kOcrVoltageDual;
        return MmcCommandResult::Short;

    case kCmdAllSendCid:
        if (before != MmcState::Ready) break;
        state_ = MmcState::Ident;
        EncodeEmmcCid(Cid(), response);
        return MmcCommandResult::Long;

    case kCmdSetRelativeAddr:
        if (before != MmcState::Ident) break;
        rca_        = arg_rca;
        state_      = MmcState::Stby;
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;

    case kCmdSendCsd:
        if (before != MmcState::Stby) break;
        if (arg_rca != rca_) return MmcCommandResult::NoResponse;
        EncodeEmmcCsd(Csd(), response);
        return MmcCommandResult::Long;

    case kCmdSendCid:
        if (before != MmcState::Stby) break;
        if (arg_rca != rca_) return MmcCommandResult::NoResponse;
        EncodeEmmcCid(Cid(), response);
        return MmcCommandResult::Long;

    case kCmdSelectCard:
        if (before == MmcState::Stby && arg_rca == rca_ && rca_ != 0u) {
            state_      = MmcState::Tran;
            response[0] = StatusWord(before);
            return MmcCommandResult::Short;
        }
        if (before == MmcState::Tran && arg_rca != rca_) {
            state_ = MmcState::Stby;
            return MmcCommandResult::NoResponse;
        }
        break;

    case kCmdSendStatus:
        if (before != MmcState::Stby && before != MmcState::Tran &&
            before != MmcState::Data) {
            break;
        }
        if (arg_rca != rca_) return MmcCommandResult::NoResponse;
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;

    case kCmdSwitch:
        if (before != MmcState::Tran) break;
        ApplySwitch(argument);
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;

    case kCmdSendExtCsd:
        if (before == MmcState::Idle) return MmcCommandResult::NoResponse;
        if (before != MmcState::Tran) break;
        BuildExtCsd();
        multi_read_ = false;
        state_      = MmcState::Data;
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;

    /* JEDEC JESD84-A43 Table 14 CMD16 SET_BLOCKLEN R1; section 8.3 READ_BL_PARTIAL: 512B
       blocks are always usable. */
    case kCmdSetBlockLen:
        if (before != MmcState::Tran || argument != kBlockBytes) break;
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;

    /* JEDEC JESD84-A43 Table 16 CMD23 SET_BLOCK_COUNT: [31] Reliable Write Request, [30:16] 0,
       [15:0] blocks for the immediately following CMD18 or CMD25 (sections 7.5.6, 7.5.7). */
    case kCmdSetBlockCount:
        if (before != MmcState::Tran) break;
        if ((argument & 0xFFFF0000u) != 0u) {
            emu_.Get<Fatal>().Die("eMMC card in slot %u: SET_BLOCK_COUNT argument 0x%08X sets reliable "
                                  "write or reserved bits, which is not modeled", SlotIndex(), argument);
        }
        block_count_ = argument;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    case kCmdReadSingleBlock:
    case kCmdReadMultiBlock:
        if (before != MmcState::Tran) break;
        if (argument >= SectorCount()) {
            response[0] = StatusWord(before) | kR1AddressOutOfRange;
            return MmcCommandResult::Short;
        }
        read_data_.resize(kBlockBytes);
        ReadBlock(argument, read_data_.data());
        multi_read_  = (index == kCmdReadMultiBlock);
        blocks_left_ = multi_read_ ? preset : 0u;
        next_sector_ = argument + 1u;
        state_       = MmcState::Data;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    case kCmdWriteBlock:
    case kCmdWriteMultiBlock:
        if (before != MmcState::Tran) break;
        if (argument >= SectorCount()) {
            response[0] = StatusWord(before) | kR1AddressOutOfRange;
            return MmcCommandResult::Short;
        }
        multi_write_ = (index == kCmdWriteMultiBlock);
        blocks_left_ = multi_write_ ? preset : 0u;
        next_sector_ = argument;
        state_       = MmcState::Rcv;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    case kCmdStopTransmission:
        if (before != MmcState::Data &&
            !(before == MmcState::Rcv && multi_write_)) {
            break;
        }
        if ((argument & kStopHpi) != 0u) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: STOP_TRANSMISSION argument 0x%08X sets "
                "the high priority interrupt bit, which is not modeled",
                SlotIndex(), argument);
        }
        multi_read_  = false;
        multi_write_ = false;
        blocks_left_ = 0u;
        state_       = MmcState::Tran;
        response[0]  = StatusWord(before);
        return MmcCommandResult::Short;

    case kCmdSetWriteProt:
        if (before != MmcState::Tran) break;
        if (argument >= SectorCount()) {
            response[0] = StatusWord(before) | kR1AddressOutOfRange;
            return MmcCommandResult::Short;
        }
        SetWriteProtect(argument);
        response[0] = StatusWord(before);
        return MmcCommandResult::Short;

    default:
        break;
    }

    HaltUnmodelledCommand(index, argument);
}

void EmmcCardBase::EndDataPhase() {
    if (state_ != MmcState::Data) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: the host finished a data phase while the "
            "card is in state %u", SlotIndex(),
            static_cast<unsigned>(state_));
    }
    if (multi_read_ && blocks_left_ == 1u) {
        multi_read_  = false;
        blocks_left_ = 0u;
    }
    if (!multi_read_) state_ = MmcState::Tran;
}

void EmmcCardBase::NextBlock() {
    if (state_ != MmcState::Data || !multi_read_) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: the host asked for another block while the "
            "card is in state %u with no multiple block read open", SlotIndex(),
            static_cast<unsigned>(state_));
    }
    if (next_sector_ >= SectorCount()) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: a multiple block read runs past the last "
            "sector into sector %u, and the error reported to the stop command "
            "is not modeled", SlotIndex(), next_sector_);
    }
    if (blocks_left_ == 1u) {
        emu_.Get<Fatal>().Die("eMMC card in slot %u: the host asked for a block past the "
                              "SET_BLOCK_COUNT count", SlotIndex());
    }
    if (blocks_left_ != 0u) --blocks_left_;
    read_data_.resize(kBlockBytes);
    ReadBlock(next_sector_, read_data_.data());
    ++next_sector_;
}

void EmmcCardBase::ReceiveBlock(const uint8_t* data, uint32_t bytes) {
    if (state_ != MmcState::Rcv) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: the host sent a %u byte data block while the "
            "card is in state %u", SlotIndex(), bytes,
            static_cast<unsigned>(state_));
    }
    if (bytes != kBlockBytes) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: the host sent a %u byte data block, and a "
            "write block length other than %u is not modeled", SlotIndex(),
            bytes, kBlockBytes);
    }
    RequireWritable(next_sector_);
    WriteBlock(next_sector_, data);
    ++next_sector_;
    if (multi_write_ && blocks_left_ != 0u && --blocks_left_ == 0u) multi_write_ = false;
    if (!multi_write_) state_ = MmcState::Tran;
}

void EmmcCardBase::Reset() {
    state_     = MmcState::Idle;
    rca_       = 0u;
    hs_timing_ = 0u;
    user_wp_   = 0u;
    erase_group_def_ = 0u;
    multi_read_  = false;
    multi_write_ = false;
    next_sector_ = 0u;
    block_count_ = 0u;
    blocks_left_ = 0u;
    power_on_wp_.assign(WpGroupCount(), 0u);
    read_data_.clear();
}

void EmmcCardBase::BuildExtCsd() {
    read_data_.assign(kExtCsdBytes, 0u);
    for (const EmmcExtCsdByte& property : ExtCsdProperties()) {
        if (IsCardOwnedExtCsdByte(property.offset)) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: the part lists extended CSD byte %u, which "
                "the card serves from its own state", SlotIndex(),
                static_cast<unsigned>(property.offset));
        }
        read_data_[property.offset] = property.value;
    }
    cerf::le::Put32(read_data_.data() + kExtCsdSecCount, SectorCount());
    read_data_[kExtCsdHsTiming]      = hs_timing_;
    read_data_[kExtCsdUserWp]        = user_wp_;
    read_data_[kExtCsdEraseGroupDef] = erase_group_def_;
    read_data_[kExtCsdErasedMemCont] = CheckedErasedMemCont();
}

// JEDEC JESD84-A43 section 7.5.8, Table 69
uint8_t EmmcCardBase::CheckedErasedMemCont() const {
    const uint8_t code = ErasedMemCont();
    if (code > kErasedMemContOnes) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: the part declares erased memory content code "
            "%u, which is reserved", SlotIndex(), static_cast<unsigned>(code));
    }
    return code;
}

uint8_t EmmcCardBase::ErasedByte() const {
    return CheckedErasedMemCont() == kErasedMemContOnes ? 0xFFu : 0x00u;
}

uint32_t EmmcCardBase::StatusWord(MmcState before) const {
    return kR1ReadyForData | (static_cast<uint32_t>(before) << kR1StateShift);
}

void EmmcCardBase::ApplySwitch(uint32_t argument) {
    const uint32_t access =
        (argument >> kSwitchAccessShift) & kSwitchAccessMask;
    const uint32_t index = (argument >> kSwitchIndexShift) & kSwitchByteMask;
    const uint32_t value = (argument >> kSwitchValueShift) & kSwitchByteMask;

    if (index == kExtCsdUserWp) {
        ApplyUserWp(access, value);
        return;
    }
    if (access != kSwitchWriteByte) {
        emu_.Get<Fatal>().Die(
            "eMMC card in slot %u: SWITCH access mode %u is not modeled",
            SlotIndex(), access);
    }
    if (index == kExtCsdBusWidth) {
        if (value == kBusWidth4BitDdr || value == kBusWidth8BitDdr) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: SWITCH selects dual data rate bus mode %u, "
                "which is not modeled", SlotIndex(), value);
        }
        if (value > kBusWidth8Bit) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: SWITCH selects bus mode %u, which is "
                "reserved", SlotIndex(), value);
        }
        return;
    }
    if (index == kExtCsdHsTiming) {
        if (value > kHsTimingHighSpeed) {
            emu_.Get<Fatal>().Die(
                "eMMC card in slot %u: SWITCH selects interface timing %u, "
                "which is not a value this card accepts", SlotIndex(), value);
        }
        hs_timing_ = static_cast<uint8_t>(value);
        return;
    }
    // JEDEC JESD84-A43 Table 72 ERASE_GROUP_DEF: ENABLE [0], [7:1] reserved
    if (index == kExtCsdEraseGroupDef) {
        if (value > 1u || std::find(power_on_wp_.begin(), power_on_wp_.end(), 1u) != power_on_wp_.end())
            emu_.Get<Fatal>().Die("eMMC card in slot %u: ERASE_GROUP_DEF 0x%02X with a protected "
                                  "group or reserved bits", SlotIndex(), value);
        erase_group_def_ = static_cast<uint8_t>(value);
        power_on_wp_.assign(WpGroupCount(), 0u);
        return;
    }
    emu_.Get<Fatal>().Die(
        "eMMC card in slot %u: SWITCH writes extended CSD byte %u, which is "
        "not modeled", SlotIndex(), index);
}

void EmmcCardBase::HaltUnmodelledCommand(uint8_t index, uint32_t argument) {
    emu_.Get<Fatal>().Die(
        "eMMC card in slot %u: CMD%u with argument 0x%08X in card state %u is "
        "not modeled", SlotIndex(), static_cast<unsigned>(index), argument,
        static_cast<unsigned>(state_));
}

void EmmcCardBase::SaveState(StateWriter& w) {
    w.Write<uint32_t>("card_state", static_cast<uint32_t>(state_));
    w.Write<uint32_t>("rca", rca_);
    w.Write<uint32_t>("hs_timing", hs_timing_);
    w.Write<uint32_t>("user_wp", user_wp_);
    w.Write<uint32_t>("multi_read", multi_read_ ? 1u : 0u);
    w.Write<uint32_t>("multi_write", multi_write_ ? 1u : 0u);
    w.Write<uint32_t>("next_sector", next_sector_);
    w.Write<uint8_t>("erase_group_def", erase_group_def_);
    w.Write("block_count", block_count_);
    w.Write("blocks_left", blocks_left_);
    w.WriteBytes("power_on_wp", power_on_wp_.data(), power_on_wp_.size());
}

void EmmcCardBase::RestoreState(StateReader& r) {
    uint32_t state     = 0u;
    uint32_t rca       = 0u;
    uint32_t hs_timing = 0u;
    uint32_t user_wp   = 0u;
    uint32_t multi     = 0u;
    uint32_t multi_w   = 0u;
    uint32_t next      = 0u;
    r.Read("card_state", state);
    r.Read("rca", rca);
    r.Read("hs_timing", hs_timing);
    r.Read("user_wp", user_wp);
    r.Read("multi_read", multi);
    r.Read("multi_write", multi_w);
    r.Read("next_sector", next);
    const bool     receiving = state == static_cast<uint32_t>(MmcState::Rcv);
    const uint32_t last_next =
        (receiving && multi_w == 0u) ? SectorCount() - 1u : SectorCount();
    if (multi > 1u || multi_w > 1u || next > last_next ||
        (multi == 1u && state != static_cast<uint32_t>(MmcState::Data)) ||
        (multi_w == 1u && !receiving)) {
        r.Reject(
            "eMMC card in slot %u: restored multiple block read %u, multiple "
            "block write %u at sector %u in state %u is not a transfer this card "
            "can hold", SlotIndex(), multi, multi_w, next, state);
    }
    multi_read_  = (multi == 1u);
    multi_write_ = (multi_w == 1u);
    next_sector_ = next;
    r.Read("erase_group_def", erase_group_def_);
    r.Read("block_count", block_count_);
    r.Read("blocks_left", blocks_left_);
    if (erase_group_def_ > 1u) r.Reject("eMMC card in slot %u: restored ERASE_GROUP_DEF %u", SlotIndex(), erase_group_def_);
    power_on_wp_.assign(WpGroupCount(), 0u);
    r.ReadBytes("power_on_wp", power_on_wp_.data(), power_on_wp_.size());
    for (const uint8_t group : power_on_wp_) {
        if (group > 1u) {
            r.Reject(
                "eMMC card in slot %u: restored write protect group state %u is "
                "not a state this card can hold", SlotIndex(),
                static_cast<unsigned>(group));
        }
    }
    if ((user_wp & ~kUserWpPwrWpEn) != 0u) {
        r.Reject(
            "eMMC card in slot %u: restored USER_WP 0x%02X sets a bit this card "
            "cannot hold", SlotIndex(), user_wp);
    }
    user_wp_ = static_cast<uint8_t>(user_wp);
    if (hs_timing > kHsTimingHighSpeed) {
        r.Reject(
            "eMMC card in slot %u: restored interface timing %u is not a value "
            "this card can hold", SlotIndex(), hs_timing);
    }
    hs_timing_ = static_cast<uint8_t>(hs_timing);
    if (state > static_cast<uint32_t>(MmcState::Rcv) || rca > 0xFFFFu) {
        r.Reject(
            "eMMC card in slot %u: restored state %u rca 0x%X is not a state "
            "this card can reach", SlotIndex(), state, rca);
    }
    state_ = static_cast<MmcState>(state);
    rca_   = static_cast<uint16_t>(rca);
}

}
