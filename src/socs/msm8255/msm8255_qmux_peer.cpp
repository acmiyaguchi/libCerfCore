#include "msm8255_qmux_peer.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>
#include <vector>

namespace {

constexpr uint8_t kQmuxMarker = 0x01u;

constexpr uint32_t kOffQmuxLength  = 1u;
constexpr uint32_t kOffQmuxFlags   = 3u;
constexpr uint32_t kOffQmuxService = 4u;
constexpr uint32_t kOffQmuxClient  = 5u;

constexpr uint8_t kQmuxFlagsFromClient  = 0x00u;
constexpr uint8_t kQmuxFlagsFromService = 0x80u;

constexpr uint8_t kServiceCtl = 0x00u;

constexpr uint32_t kOffCtlFlags     = 6u;
constexpr uint32_t kOffCtlTxn       = 7u;
constexpr uint32_t kOffCtlMessage   = 8u;
constexpr uint32_t kOffCtlTlvLength = 10u;
constexpr uint32_t kCtlFrameBytes   = 12u;

constexpr uint8_t kCtlFlagsRequest  = 0x00u;
constexpr uint8_t kCtlFlagsResponse = 0x01u;

constexpr uint16_t kCtlMessageSync = 0x0027u;

constexpr uint8_t  kTlvResult      = 0x02u;
constexpr uint16_t kResultSuccess  = 0x0000u;
constexpr uint16_t kErrorNone      = 0x0000u;

constexpr uint32_t kTlvHeaderBytes = 3u;
constexpr uint16_t kResultTlvBytes = 4u;
constexpr uint32_t kSyncTlvBytes   = kTlvHeaderBytes + kResultTlvBytes;
constexpr uint32_t kSyncReplyBytes = kCtlFrameBytes + kSyncTlvBytes;

}  // namespace

REGISTER_SERVICE(Msm8255QmuxPeer);

bool Msm8255QmuxPeer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

uint32_t Msm8255QmuxPeer::Answer(uint32_t in_pa, uint32_t in_bytes,
                                 uint32_t out_pa, uint32_t out_cap) {
    auto& mem = emu_.Get<EmulatedMemory>();

    if (in_bytes < kCtlFrameBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: a %u-byte packet is short of the %u-byte "
            "control frame", in_bytes, kCtlFrameBytes);
    }

    uint8_t frame[kCtlFrameBytes] = {};
    mem.CopyOut(in_pa, frame, kCtlFrameBytes);

    if (frame[0] != kQmuxMarker) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: interface marker 0x%02X is not modeled",
            frame[0]);
    }
    const uint32_t qmux_len = cerf::le::U16(frame, kOffQmuxLength);
    if (qmux_len != in_bytes - 1u) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: the qmux header declares %u bytes in a %u-byte "
            "packet", qmux_len, in_bytes);
    }
    if (frame[kOffQmuxFlags] != kQmuxFlagsFromClient) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: qmux flags 0x%02X are not modeled",
            frame[kOffQmuxFlags]);
    }
    const uint8_t service = frame[kOffQmuxService];
    const uint8_t client  = frame[kOffQmuxClient];
    if (service != kServiceCtl) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: qmi service %u (client %u) is not modeled",
            service, client);
    }

    const uint8_t  ctl_flags = frame[kOffCtlFlags];
    const uint8_t  txn       = frame[kOffCtlTxn];
    const uint16_t message   = cerf::le::U16(frame, kOffCtlMessage);
    const uint32_t tlv_len   = cerf::le::U16(frame, kOffCtlTlvLength);
    if (ctl_flags != kCtlFlagsRequest) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: ctl flags 0x%02X on message 0x%04X are not "
            "modeled", ctl_flags, message);
    }
    if (tlv_len != in_bytes - kCtlFrameBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: ctl message 0x%04X declares %u tlv bytes in a "
            "%u-byte packet", message, tlv_len, in_bytes);
    }
    if (message != kCtlMessageSync) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: ctl message 0x%04X with %u tlv bytes is not "
            "modeled", message, tlv_len);
    }
    if (tlv_len != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: ctl sync carries %u tlv bytes, which are not "
            "modeled", tlv_len);
    }
    if (out_cap < kSyncReplyBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 qmux peer: the reply needs %u bytes and the window at "
            "0x%08X has %u", kSyncReplyBytes, out_pa, out_cap);
    }

    std::vector<uint8_t> reply;
    reply.reserve(kSyncReplyBytes);
    reply.push_back(kQmuxMarker);
    cerf::le::Append16(reply, static_cast<uint16_t>(kSyncReplyBytes - 1u));
    reply.push_back(kQmuxFlagsFromService);
    reply.push_back(kServiceCtl);
    reply.push_back(client);
    reply.push_back(kCtlFlagsResponse);
    reply.push_back(txn);
    cerf::le::Append16(reply, kCtlMessageSync);
    cerf::le::Append16(reply, static_cast<uint16_t>(kSyncTlvBytes));
    reply.push_back(kTlvResult);
    cerf::le::Append16(reply, kResultTlvBytes);
    cerf::le::Append16(reply, kResultSuccess);
    cerf::le::Append16(reply, kErrorNone);

    mem.CopyIn(out_pa, reply.data(), reply.size());
    return static_cast<uint32_t>(reply.size());
}
