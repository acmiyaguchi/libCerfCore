#include "imx28_i2c_master.h"

#include <cerfcore/core/fatal.h>

namespace cerfcore {

void Imx28I2cMaster::Reset() {
    busy_    = false;
    reading_ = false;
    addr_    = 0u;
}

void Imx28I2cMaster::Die(const char* what, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 %08X: I2C %s %08X (bus busy %u, reading %u, address %02X)",
                          mmio_base_, what, value, busy_ ? 1u : 0u, reading_ ? 1u : 0u, addr_);
}

/* MCIMX28RM Rev. 2 section 27.2.2.1 Tables 27-2, 27-12; Table 27-1 No Slave Ack: the bus returns
   to idle. Returns false when no slave acknowledges the address byte. */
bool Imx28I2cMaster::Transmit(const Command& c, const uint8_t* data) {
    if (c.count == 0u) Die("transmit command with XFER_COUNT", c.count);
    uint32_t i = 0u;
    if (c.start) {
        const uint8_t sad = data[0];
        i        = 1u;
        addr_    = static_cast<uint8_t>(sad >> 1);
        reading_ = (sad & 1u) != 0u;
        if (slave_ == nullptr || !slave_->MatchesAddress(addr_)) {
            busy_ = false;
            return false;
        }
        slave_->TxnStart(addr_);
        busy_ = true;
        if (reading_ && c.count != 1u) Die("data bytes after SAD+R, XFER_COUNT", c.count);
    } else if (!busy_ || reading_) {
        Die("transmit without a write phase, XFER_COUNT", c.count);
    }
    for (; i < c.count; ++i) slave_->TxnWriteByte(addr_, data[i]);
    if (c.stop) busy_ = false;
    return true;
}

/* MCIMX28RM Rev. 2 section 27.2.2.1 Table 27-11. */
void Imx28I2cMaster::Receive(const Command& c, uint8_t* out) {
    if (c.count == 0u) Die("receive command with XFER_COUNT", c.count);
    if (c.start || !busy_ || !reading_) Die("receive without a read phase, XFER_COUNT", c.count);
    for (uint32_t i = 0; i < c.count; ++i) out[i] = slave_->TxnReadByte(addr_);
    if (c.stop) busy_ = false;
}

void Imx28I2cMaster::Save(StateWriter& w) const {
    w.Write<uint8_t>("busy", busy_ ? 1u : 0u);
    w.Write<uint8_t>("reading", reading_ ? 1u : 0u);
    w.Write("addr", addr_);
    if (slave_ != nullptr) slave_->SaveState(w);
}

void Imx28I2cMaster::Restore(StateReader& r) {
    uint8_t b = 0u;
    r.Read("busy", b);
    busy_ = b != 0u;
    r.Read("reading", b);
    reading_ = b != 0u;
    r.Read("addr", addr_);
    if (slave_ != nullptr) slave_->RestoreState(r);
}

}
