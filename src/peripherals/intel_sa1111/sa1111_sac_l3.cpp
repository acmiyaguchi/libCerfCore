#include "sa1111_sac_l3.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "../../boards/hp_jornada_700/hp_jornada_700_family_id.h"
#include <cerfcore/state/state_stream.h>

namespace {

/* Table 7-10 (printed 7-13): SASR0 L3WD 16, L3RD 17; Table 7-12 (printed 7-15): SASCR DTS 16,
   RDD 17. */
constexpr uint32_t kL3Wd = 1u << 16;
constexpr uint32_t kL3Rd = 1u << 17;

/* §7.4.6 (printed 7-16): "If both address LSBs are "01", it is a read request from the codec". */
constexpr uint32_t kTransferTypeMask = 3u;
constexpr uint32_t kReadTransfer     = 1u;

constexpr uint8_t kUndrivenL3Data = 0u;

/* Table 7-8 (printed 7-11): L3EN "1 = Pins function as L3 bus signals"; L3MB "0 = L3 Control
   Bus Data is Last Byte Transfer", "1 = L3 Control Bus Data is Multiple Byte Transfer". */
constexpr uint32_t kSacr1L3En = 1u << 1;
constexpr uint32_t kSacr1L3Mb = 1u << 2;

}

bool Sa1111SacL3::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

/* Table 7-10 (printed 7-13): L3WD "1 - L3 Control Bus Data Write Data is Done", L3RD "1 - L3
   Control Bus Data Read Data is Done". */
void Sa1111SacL3::Settle() {
    if (!running_) return;
    if (data_) {
        data_ = false;
        sent_ = true;
    }
    if (read_) {
        read_     = false;
        received_ = true;
        rx_       = kUndrivenL3Data;
    }
}

void Sa1111SacL3::WriteAddress(uint32_t value) {
    car_       = value & 0xFFu;
    data_      = false;
    read_      = (car_ & kTransferTypeMask) == kReadTransfer;
    Settle();
}

/* §7.4.6: "When the L3 Data Register is subsequently written, the register's contents are
   transmitted serially to the codec." */
void Sa1111SacL3::WriteData(uint32_t value, uint32_t sacr1) {
    const char* byte = (sacr1 & kSacr1L3Mb) != 0u ? "multiple" : "last";
    if ((sacr1 & kSacr1L3En) != 0u) {
        LOG(Periph, "[Sa1111Sac] L3 codec write addr=0x%02X val=0x%02X (%s byte)\n", car_,
            value & 0xFFu, byte);
    } else {
        LOG(Periph, "[Sa1111Sac] L3 data byte 0x%02X (%s byte) with SACR1 L3EN clear, not on "
            "the L3 pins\n", value & 0xFFu, byte);
    }
    data_      = true;
    Settle();
}

uint32_t Sa1111SacL3::StatusBits() const {
    return (sent_ ? kL3Wd : 0u) | (received_ ? kL3Rd : 0u);
}

void Sa1111SacL3::ClearStatus(uint32_t sascr) {
    if ((sascr & kL3Wd) != 0u) sent_ = false;
    if ((sascr & kL3Rd) != 0u) received_ = false;
}

/* SA-1111 Developer's Manual §5.2.1 L3CLKEn "Audio Controller L3 Clock Enable". */
void Sa1111SacL3::OnClockChange(bool running) {
    running_ = running;
    Settle();
}

/* §2.3 (printed 2-4): "When nRESET is asserted, all on-chip activity halts". */
void Sa1111SacL3::Reset() {
    car_       = 0u;
    data_      = false;
    sent_      = false;
    read_      = false;
    received_  = false;
    rx_        = 0u;
}

void Sa1111SacL3::Save(StateWriter& w) {
    w.Write("l3car", car_);
    w.Write<uint8_t>("l3_data", data_ ? 1u : 0u);
    w.Write<uint8_t>("l3wd", sent_ ? 1u : 0u);
    w.Write<uint8_t>("l3_read", read_ ? 1u : 0u);
    w.Write<uint8_t>("l3rd", received_ ? 1u : 0u);
    w.Write<uint8_t>("l3_rx", rx_);
}

void Sa1111SacL3::Restore(StateReader& r) {
    uint8_t data = 0u, sent = 0u, read = 0u, received = 0u;
    r.Read("l3car", car_);
    r.Read("l3_data", data);
    r.Read("l3wd", sent);
    r.Read("l3_read", read);
    r.Read("l3rd", received);
    r.Read("l3_rx", rx_);
    data_      = data != 0u;
    sent_      = sent != 0u;
    read_      = read != 0u;
    received_  = received != 0u;
}

REGISTER_SERVICE(Sa1111SacL3);
