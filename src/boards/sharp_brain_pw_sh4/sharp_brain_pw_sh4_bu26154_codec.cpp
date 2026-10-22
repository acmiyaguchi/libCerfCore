#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "../../socs/i2c_slave.h"
#include "sharp_brain_pw_sh4_bu26154_registers.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* BU26154MUV datasheet Rev.002 "2 wire serial interface": address 0011010 with SAD low. */
constexpr uint8_t kAddress = 0x1Au;

/* Register map: MAPCON read index 0x1C write index 0x1D, values 0x0 to 0x2; SOFTRST read index
   0x10 write index 0x11 in MAPCON 0x0. */
constexpr uint8_t kMapconRead  = 0x1Cu;
constexpr uint8_t kMapconMask  = 0x03u;
constexpr uint8_t kMapCount    = 3u;
constexpr uint8_t kSoftrstRead = 0x10u;
/* sharp_brain_pw_sh4 wavedev2_BU26154.dll c07ab4e8 (table c07a2e70+0x54), c07ab1c0;
   sharp_brain_pw_sh4 nk.exe OAL_BeepSound 8021435c. */
constexpr uint8_t kMap2WriteOnlyRead = 0x20u;

/* BU26154MUV datasheet Rev.002 2 wire serial interface: continuous access steps by two. */
constexpr uint8_t kIndexStep = 2u;

class SharpBrainPwSh4Bu26154Codec : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override { Reset(); }

    bool MatchesAddress(uint8_t slave_addr) const override { return slave_addr == kAddress; }

    void    TxnStart(uint8_t) override { index_pending_ = true; }
    void    TxnWriteByte(uint8_t, uint8_t byte) override;
    uint8_t TxnReadByte(uint8_t) override;

    void SaveState(StateWriter& w) override {
        w.Write("regs", regs_);
        w.Write("map", map_);
        w.Write("index", index_);
        w.Write<uint8_t>("index_pending", index_pending_ ? 1u : 0u);
    }

    void RestoreState(StateReader& r) override {
        uint8_t pending = 0u;
        r.Read("regs", regs_);
        r.Read("map", map_);
        r.Read("index", index_);
        r.Read("index_pending", pending);
        index_pending_ = pending != 0u;
    }

private:
    void Reset();
    const SharpBrainPwSh4Bu26154Register* Find(uint8_t read_index) const;
    [[noreturn]] void Die(const char* what, uint8_t index, uint8_t value) const;

    uint8_t regs_[kMapCount][256] = {};
    uint8_t map_           = 0u;
    uint8_t index_         = 0u;
    bool    index_pending_ = true;
};

void SharpBrainPwSh4Bu26154Codec::Reset() {
    for (const SharpBrainPwSh4Bu26154Register& r : kSharpBrainPwSh4Bu26154Registers) {
        regs_[r.map][r.read_index] = r.reset;
    }
    map_           = 0u;
    index_         = 0u;
    index_pending_ = true;
}

void SharpBrainPwSh4Bu26154Codec::Die(const char* what, uint8_t index, uint8_t value) const {
    emu_.Get<Fatal>().Die("SharpBrainPwSh4Bu26154Codec: %s index %02X value %02X (map %u)", what,
                          index, value, map_);
}

const SharpBrainPwSh4Bu26154Register* SharpBrainPwSh4Bu26154Codec::Find(uint8_t read_index) const {
    for (const SharpBrainPwSh4Bu26154Register& r : kSharpBrainPwSh4Bu26154Registers) {
        if (r.map == map_ && r.read_index == read_index) return &r;
    }
    return nullptr;
}

void SharpBrainPwSh4Bu26154Codec::TxnWriteByte(uint8_t, uint8_t byte) {
    if (index_pending_) {
        index_         = byte;
        index_pending_ = false;
        return;
    }
    const uint8_t at = index_;
    index_ = static_cast<uint8_t>(index_ + kIndexStep);
    if ((at & 1u) == 0u) Die("write to a read index", at, byte);
    const uint8_t read_index = static_cast<uint8_t>(at - 1u);
    if (read_index == kMapconRead) {
        if ((byte & ~kMapconMask) != 0u || (byte & kMapconMask) >= kMapCount) {
            Die("MAPCON write", at, byte);
        }
        map_ = byte;
        return;
    }
    if (map_ == 2u && read_index == kMap2WriteOnlyRead) {
        regs_[map_][read_index] = byte;
        return;
    }
    const SharpBrainPwSh4Bu26154Register* reg = Find(read_index);
    if (reg == nullptr) Die("write to an undefined register", at, byte);
    /* Datasheet Rev.002 "Register map" note and MAPCON 0x0 index 0x26: b2 is H fix. */
    if (((byte ^ reg->reset) & ~reg->mask) != 0u) Die("write to reserved bits", at, byte);
    if (map_ == 0u && read_index == kSoftrstRead && byte != 0u) Die("SOFTRST write", at, byte);
    regs_[map_][read_index] = byte;
}

uint8_t SharpBrainPwSh4Bu26154Codec::TxnReadByte(uint8_t) {
    const uint8_t at = index_;
    index_ = static_cast<uint8_t>(index_ + kIndexStep);
    if ((at & 1u) != 0u) Die("read from a write index", at, 0u);
    if (at == kMapconRead) return map_;
    if (Find(at) == nullptr) Die("read from an undefined register", at, 0u);
    return regs_[map_][at];
}

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4Bu26154Codec, I2cSlave);

}
