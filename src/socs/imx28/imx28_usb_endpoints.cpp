#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "imx28_usb_endpoints.h"

#include <algorithm>
#include <cstdint>

namespace cerfcore {

namespace {

/* Section 31.2.6.5.1 Table 31-53: 64-byte dQH per endpoint direction, RX then TX; capabilities
   at +0x00, Current dTD at +0x04, overlay from Next dTD at +0x08 through Page 4, setup buffer at
   +0x28. */
constexpr uint32_t kQhStride     = 0x40u;
constexpr uint32_t kQhCurrentDtd = 0x04u;
constexpr uint32_t kQhOverlay    = 0x08u;
constexpr uint32_t kQhToken      = 0x0Cu;
constexpr uint32_t kQhPage0      = 0x10u;
constexpr uint32_t kQhSetup      = 0x28u;
constexpr uint32_t kOverlayWords = 7u;
constexpr uint32_t kPages        = 5u;

/* Table 31-54: ZLT [29], Maximum Packet Length [26:16], IOS [15]. */
constexpr uint32_t kCapZltOff  = 1u << 29;
constexpr uint32_t kCapMpsMask = 0x7FFu;
constexpr uint32_t kCapIos     = 1u << 15;

/* Tables 31-58 and 31-59: T [0], pointer [31:5]; Total Bytes [30:16], IOC [15], Status [7:0]
   with Active [7]. */
constexpr uint32_t kDtdTerminate   = 1u << 0;
constexpr uint32_t kDtdPointerMask = 0xFFFFFFE0u;
constexpr uint32_t kTokenTotalMask = 0x7FFFu;
constexpr uint32_t kTokenIoc       = 1u << 15;
constexpr uint32_t kTokenStatus    = 0xFFu;
constexpr uint32_t kTokenActive    = 1u << 7;
constexpr uint32_t kPageMask       = 0xFFFFF000u;
constexpr uint32_t kPageSize       = 0x1000u;

uint32_t QueueHead(uint32_t list, uint32_t bit) {
    return list + ((bit & 0x7u) * 2u + (bit >= 16u ? 1u : 0u)) * kQhStride;
}

}  /* namespace */

void Imx28UsbEndpoints::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 %08X: %s at %08X (value %08X)", base_, what, addr, value);
}

uint32_t Imx28UsbEndpoints::Read(uint32_t addr) {
    uint32_t v = 0u;
    if (emu_.Get<EmulatedMemory>().TryCopyOut(addr, reinterpret_cast<uint8_t*>(&v), 4u) != 4u) {
        Die("dQH/dTD read failed", addr, 0u);
    }
    return v;
}

void Imx28UsbEndpoints::Write(uint32_t addr, uint32_t value) {
    if (emu_.Get<EmulatedMemory>().TryCopyIn(addr, reinterpret_cast<const uint8_t*>(&value), 4u) != 4u) {
        Die("dQH/dTD write failed", addr, value);
    }
}

/* Sections 31.3.36 and 31.2.6.6.2.6.3: priming copies the dTD named by the dQH into the overlay
   and sets the Current dTD pointer. */
void Imx28UsbEndpoints::Fetch(uint32_t list, uint32_t bit, uint32_t value) {
    const uint32_t qh   = QueueHead(list, bit);
    const uint32_t next = Read(qh + kQhOverlay);
    if ((next & kDtdTerminate) != 0u) Die("ENDPTPRIME with terminated dTD list", qh, next);
    const uint32_t dtd = next & kDtdPointerMask;
    uint32_t overlay[kOverlayWords] = {};
    auto& mem = emu_.Get<EmulatedMemory>();
    auto* raw = reinterpret_cast<uint8_t*>(overlay);
    if (mem.TryCopyOut(dtd, raw, sizeof(overlay)) != sizeof(overlay) ||
        mem.TryCopyIn(qh + kQhOverlay, raw, sizeof(overlay)) != sizeof(overlay)) {
        Die("ENDPTPRIME dTD fetch failed", dtd, value);
    }
    Write(qh + kQhCurrentDtd, dtd);
}

/* Section 31.2.6.5.1.4 Table 31-56: setup bytes 3..0 and 7..4 in the RX dQH. */
void Imx28UsbEndpoints::WriteSetup(uint32_t list, const uint8_t* setup) {
    const uint32_t qh = QueueHead(list, 0u);
    if (emu_.Get<EmulatedMemory>().TryCopyIn(qh + kQhSetup, setup, 8u) != 8u) {
        Die("setup buffer write failed", qh + kQhSetup, 0u);
    }
}

bool Imx28UsbEndpoints::InterruptOnSetup(uint32_t list) {
    return (Read(QueueHead(list, 0u)) & kCapIos) != 0u;
}

/* Table 31-60: Page 0 holds the current offset, Pages 1-4 the following 4 KB pages. */
void Imx28UsbEndpoints::Copy(uint32_t qh, uint8_t* data, uint32_t length, bool tx) {
    auto&    mem  = emu_.Get<EmulatedMemory>();
    uint32_t addr = Read(qh + kQhPage0);
    uint32_t pos  = 0u;
    for (uint32_t page = 1u; pos < length; ++page) {
        const uint32_t n = std::min(kPageSize - (addr & ~kPageMask), length - pos);
        const uint32_t done = tx ? mem.TryCopyOut(addr, data + pos, n) : mem.TryCopyIn(addr, data + pos, n);
        if (done != n) Die("dTD buffer access failed", addr, n);
        pos += n;
        if (pos < length) {
            if (page >= kPages) Die("dTD buffer past Page 4", qh, length);
            addr = Read(qh + kQhPage0 + page * 4u) & kPageMask;
        }
    }
}

/* Sections 31.2.6.5.1.2, 31.2.6.6.5.4, Table 31-54 ZLT and Table 31-59: the dTD moves its bytes,
   Total Bytes drops by the moved count, Active clears, results go back to the dTD and the queue
   advances to the next dTD. */
Imx28UsbEndpoints::Transfer Imx28UsbEndpoints::Execute(uint32_t list, uint32_t bit, uint8_t* data,
                                                       uint32_t length) {
    const bool     tx    = bit >= 16u;
    const uint32_t qh    = QueueHead(list, bit);
    const uint32_t cap   = Read(qh);
    const uint32_t mps   = (cap >> 16) & kCapMpsMask;
    const uint32_t token = Read(qh + kQhToken);
    if ((token & kTokenStatus) != kTokenActive) Die("dTD status not Active only", qh, token);
    if (mps == 0u) Die("dQH Maximum Packet Length 0", qh, cap);
    const uint32_t total = (token >> 16) & kTokenTotalMask;
    if (tx && total > length) Die("IN dTD Total Bytes above host length", qh, token);
    if (!tx && length > total) Die("OUT data above dTD Total Bytes", qh, token);
    const uint32_t moved = tx ? total : length;
    const bool     shortp = (moved % mps) != 0u || moved == 0u || (tx && (cap & kCapZltOff) == 0u);
    if (!tx && !shortp && moved < total) Die("OUT full packets below dTD Total Bytes", qh, token);
    Copy(qh, data, moved, tx);
    const uint32_t done = (token & ~((kTokenTotalMask << 16) | kTokenStatus)) | ((total - moved) << 16);
    Write(qh + kQhToken, done);
    Write(Read(qh + kQhCurrentDtd) + 4u, done);
    const bool more = (Read(qh + kQhOverlay) & kDtdTerminate) == 0u;
    if (more) Fetch(list, bit, 0u);
    return Transfer{moved, (token & kTokenIoc) != 0u, shortp, more};
}

}
