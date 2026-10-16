#include "sa1111_sac_dma_control.h"

#include <cerfcore/state/state_stream.h>

namespace {

/* SA-1111 Developer's Manual Table 3-3 note 1: "All reserved bits are read back as zero."
   Tables 7-21 / 7-23 / 7-26 / 7-28 count 12:0. */
constexpr uint32_t kCountMask = 0x1FFFu;

}

void Sa1111SacDmaControl::WriteControl(uint32_t value) {
    uint32_t& cs = regs_[kControl];
    cs &= ~(value & (kBda | kBdb));
    cs  = (cs & ~(kEn | kIe)) | (value & (kEn | kIe));
    cs |= value & (kSta | kStb);
    if (!Enabled()) cs &= ~(kSta | kStb | kBiu);
}

void Sa1111SacDmaControl::WriteBuffer(uint32_t index, uint32_t value) {
    regs_[index] = (index == kCountA || index == kCountB) ? (value & kCountMask) : value;
}

uint32_t Sa1111SacDmaControl::Address(bool buffer_b) const {
    return regs_[buffer_b ? kAddressB : kAddressA] & ~0x3u;
}

uint32_t Sa1111SacDmaControl::Bytes(bool buffer_b) const {
    return regs_[buffer_b ? kCountB : kCountA] & ~0x3u;
}

bool Sa1111SacDmaControl::NextBuffer(bool prefer_b, bool& buffer_b) {
    const uint32_t cs = regs_[kControl];
    if (prefer_b && (cs & kStb) != 0u) { buffer_b = true;  return true; }
    if ((cs & kSta) != 0u)             { buffer_b = false; return true; }
    if ((cs & kStb) != 0u)             { buffer_b = true;  return true; }
    Idle();
    return false;
}

void Sa1111SacDmaControl::Begin(bool buffer_b) {
    regs_[kControl] = (regs_[kControl] & ~kBiu) | (buffer_b ? kBiu : 0u);
}

void Sa1111SacDmaControl::Idle() { regs_[kControl] &= ~kBiu; }

/* SA-1111 Developer's Manual §7.2.2 (printed 7-5): "When the complete block is transferred, it
   sets the appropriate DMA_Done bit in the status register. An interrupt will be signaled to
   the system processor if it is enabled." */
bool Sa1111SacDmaControl::Complete(bool buffer_b, bool keep_started) {
    regs_[kControl] |= buffer_b ? kBdb : kBda;
    if (!keep_started) regs_[kControl] &= ~(buffer_b ? kStb : kSta);
    return InterruptEnabled();
}

void Sa1111SacDmaControl::Reset() {
    for (uint32_t& reg : regs_) reg = 0u;
}

void Sa1111SacDmaControl::Save(StateWriter& w) const {
    w.Write(keys_.control,   regs_[kControl]);
    w.Write(keys_.address_a, regs_[kAddressA]);
    w.Write(keys_.count_a,   regs_[kCountA]);
    w.Write(keys_.address_b, regs_[kAddressB]);
    w.Write(keys_.count_b,   regs_[kCountB]);
}

void Sa1111SacDmaControl::Restore(StateReader& r) {
    r.Read(keys_.control,   regs_[kControl]);
    r.Read(keys_.address_a, regs_[kAddressA]);
    r.Read(keys_.count_a,   regs_[kCountA]);
    r.Read(keys_.address_b, regs_[kAddressB]);
    r.Read(keys_.count_b,   regs_[kCountB]);
}
