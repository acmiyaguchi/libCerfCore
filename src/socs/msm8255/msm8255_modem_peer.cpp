#include "msm8255_modem_peer.h"

#include "msm8255_dal_remote_server.h"
#include "msm8255_qmux_peer.h"
#include "msm8255_rpc_router_peer.h"
#include "msm8255_smd_stage.h"
#include "msm8255_smem.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../irq_controller.h"

#include <cstdint>
#include <cstring>

namespace cerfcore {

namespace {

/* Linux arch/arm/mach-msm proc_comm.c: APP_COMMAND 0x00, MDM_STATUS 0x14. */
constexpr uint32_t kAppCommandOff = 0x00u;
constexpr uint32_t kMdmStatusOff  = 0x14u;

/* Linux arch/arm/mach-msm proc_comm.h: PCOM_READY. */
constexpr uint32_t kPcomReady = 1u;

/* Linux arch/arm/mach-msm smd_private.h: SMEM_SMSM_SHARED_STATE evaluated over
   its enum with SMEM_NUM_SMD_CHANNELS 64, and SMSM_STATE_MODEM from the
   non-MSM7X00A enum smsm_state_item. */
constexpr uint32_t kIdSmsmSharedState = 0x55u;
constexpr uint32_t kSmsmStateModem    = 1u;

/* Linux arch/arm/mach-msm smd_private.h SMSM_V1_SIZE, the eight-entry shared
   state that smd.c smd_core_init accepts. */
constexpr uint32_t kSmsmStateBytes = 32u;

/* Linux arch/arm/mach-msm smd.c publishes SMSM_INIT | SMSM_SMDINIT |
   SMSM_RPCINIT | SMSM_RUN for a processor that is up and running. */
constexpr uint32_t kSmsmModemUp = 0x129u;

/* Linux arch/arm/mach-msm smd_private.h msm_a2m_int writes 1 << irq to
   MSM_GCC_BASE + 0x8; proc_comm.c uses irq 6 and smd.c uses irqs 0 and 5. */
constexpr uint32_t kA2mSmdModem = 1u << 0;
constexpr uint32_t kA2mSmsm     = 1u << 5;
constexpr uint32_t kA2mProcComm = 1u << 6;

/* Linux arch/arm/mach-msm irqs-7x30.h INT_A9_M2A_0 and INT_A9_M2A_5, which
   smd.c smd_core_init requests IRQF_TRIGGER_RISING for smd_modem_irq_handler
   and smsm_irq_handler. */
constexpr int kVicM2aSmd  = 22;
constexpr int kVicM2aSmsm = 27;

/* Linux arch/arm/mach-msm smd_private.h: SMEM_CHANNEL_ALLOC_TBL,
   SMEM_SMD_BASE_ID and SMEM_SMD_FIFO_BASE_ID evaluated over its enum with
   SMEM_NUM_SMD_CHANNELS 64, struct smd_alloc_elm, and SMD_CHANNELS. */
constexpr uint32_t kIdChannelAllocTbl = 13u;
constexpr uint32_t kIdSmdBase         = 14u;
constexpr uint32_t kIdSmdFifoBase     = 338u;

constexpr uint32_t kFifoWindowAlign = 0x1Fu;
constexpr uint32_t kFifoWindowMin   = 0x400u;
constexpr uint32_t kFifoWindowMax   = 0x10000u;

static_assert(Msm8255SmdStage::WriteCapacity() >= kFifoWindowMax,
              "the write stage must cover the largest accepted fifo direction");

constexpr uint32_t kSmdChannels       = 64u;
constexpr uint32_t kAllocElmBytes     = 32u;
constexpr uint32_t kAllocElmNameBytes = 20u;
constexpr uint32_t kAllocElmCidOff    = 20u;
constexpr uint32_t kAllocElmCtypeOff  = 24u;
constexpr uint32_t kAllocElmRefOff    = 28u;

/* Linux arch/arm/mach-msm smd_private.h: struct smd_half_channel and
   struct smd_shared_v2, whose ch0 and ch1 are one half-channel apart. */
constexpr uint32_t kHalfChannelBytes = 20u;
constexpr uint32_t kSmdSharedBytes   = kHalfChannelBytes * 2u;
constexpr uint32_t kHcFDsrOff        = 4u;
constexpr uint32_t kHcFCtsOff        = 5u;
constexpr uint32_t kHcFCdOff         = 6u;
constexpr uint32_t kHcFHeadOff       = 8u;
constexpr uint32_t kHcFTailOff       = 9u;
constexpr uint32_t kHcFStateOff      = 10u;
constexpr uint32_t kHcTailOff        = 12u;
constexpr uint32_t kHcHeadOff        = 16u;

/* Linux arch/arm/mach-msm smd.c smd_stream_write_avail answers
   fifo_mask - ((head - tail) & fifo_mask), leaving one byte unwritten so that
   head meeting tail always reads as empty. */
constexpr uint32_t SmdWriteAvail(uint32_t half, uint32_t head, uint32_t tail) {
    return half - 1u - ((head + half - tail) % half);
}

/* Linux arch/arm/mach-msm smd_private.h: SMD_SS_*, SMD_TYPE_MASK and
   SMD_TYPE_APPS_MODEM. */
constexpr uint32_t kSmdSsClosed       = 0u;
constexpr uint32_t kSmdSsOpening      = 1u;
constexpr uint32_t kSmdSsOpened       = 2u;
constexpr uint32_t kSmdTypeMask       = 0xFFu;
constexpr uint32_t kSmdTypeAppsModem  = 0x00u;

/* Linux arch/arm/mach-msm smd_private.h: SMD_KIND_MASK, SMD_KIND_UNKNOWN,
   SMD_KIND_STREAM and SMD_KIND_PACKET. */
constexpr uint32_t kSmdKindMask    = 0xF00u;
constexpr uint32_t kSmdKindUnknown = 0x000u;
constexpr uint32_t kSmdKindStream  = 0x100u;
constexpr uint32_t kSmdKindPacket  = 0x200u;

/* Linux arch/arm/mach-msm smd.c smd_packet_write: hdr[0] = len and
   hdr[1..4] = 0 ahead of the payload. */
constexpr uint32_t kSmdPacketHeaderBytes = 20u;

constexpr uint32_t kAppsWriterSlackBytes = 4u;

constexpr uint32_t kRpcRouterCid = 2u;

constexpr char kDalPortName[] = "DAL0";

constexpr const char* kQmuxControlPorts[] = {"DATA5_CNTL", "DATA6_CNTL",
                                             "DATA7_CNTL"};

}

bool Msm8255ModemPeer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255ModemPeer::OnReady() {
    irq_ = &emu_.Get<IrqController>();
    SeedProcCommReady();
    emu_.Get<GuestColdBoot>().RegisterReplay([this] { SeedProcCommReady(); });
}

void Msm8255ModemPeer::RingDoorbell(uint32_t mask) {
    switch (mask) {
    case kA2mSmdModem: NotifySmd();         return;
    case kA2mSmsm:     PublishModemState(); return;
    case kA2mProcComm: RunProcComm();       return;
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: apps-to-modem doorbell mask 0x%03X is not "
            "modeled", mask);
    }
}

void Msm8255ModemPeer::SeedProcCommReady() {
    auto& smem = emu_.Get<Msm8255Smem>();
    emu_.Get<EmulatedMemory>().WriteWord(smem.SmemPa() + kMdmStatusOff,
                                         kPcomReady);
}

void Msm8255ModemPeer::PublishModemState() {
    auto& smem = emu_.Get<Msm8255Smem>();
    const uint32_t state = smem.ItemPa(kIdSmsmSharedState, kSmsmStateBytes);
    if (state == 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: the guest rang the SMSM doorbell before "
            "allocating smem item %u", kIdSmsmSharedState);
    }
    emu_.Get<EmulatedMemory>().WriteWord(state + 4u * kSmsmStateModem,
                                         kSmsmModemUp);
    irq_->PulseIrq(kVicM2aSmsm);
}

void Msm8255ModemPeer::NotifySmd() {
    auto& smem = emu_.Get<Msm8255Smem>();
    auto& mem  = emu_.Get<EmulatedMemory>();
    const uint32_t tbl =
        smem.ItemPa(kIdChannelAllocTbl, kAllocElmBytes * kSmdChannels);
    if (tbl == 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: the guest rang the SMD doorbell before "
            "allocating smem item %u", kIdChannelAllocTbl);
    }

    for (uint32_t n = 0; n < kSmdChannels; ++n) {
        const uint32_t rec = tbl + kAllocElmBytes * n;
        if (mem.ReadWord(rec + kAllocElmRefOff) == 0u) continue;
        if (mem.ReadByte(rec) == 0u) continue;

        const uint32_t ctype = mem.ReadWord(rec + kAllocElmCtypeOff);
        if ((ctype & kSmdTypeMask) != kSmdTypeAppsModem) continue;

        const uint32_t cid = mem.ReadWord(rec + kAllocElmCidOff);
        if (cid >= kSmdChannels) {
            emu_.Get<Fatal>().Die(
                "msm8255 modem peer: smd channel %u names cid %u, which is "
                "outside the %u-channel table", n, cid, kSmdChannels);
        }

        const uint32_t item = smem.ItemPa(kIdSmdBase + cid, kSmdSharedBytes);
        if (item == 0u) {
            emu_.Get<Fatal>().Die(
                "msm8255 modem peer: smd channel %u names cid %u, whose smem "
                "item %u is not allocated", n, cid, kIdSmdBase + cid);
        }

        ServiceSmdChannel(cid, rec, item);
    }
}

void Msm8255ModemPeer::ServiceSmdChannel(uint32_t cid, uint32_t rec,
                                         uint32_t item) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t apps_half  = item;
    const uint32_t modem_half = item + kHalfChannelBytes;

    const uint32_t apps_state = mem.ReadWord(apps_half);
    if (apps_state == kSmdSsClosed) return;
    if (apps_state != kSmdSsOpening && apps_state != kSmdSsOpened) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: apps smd half-channel state %u is not modeled",
            apps_state);
    }

    if (apps_state == kSmdSsOpened &&
        mem.ReadWord(modem_half) == kSmdSsOpened) {
        ConsumeAppsSmdFlags(cid, rec, apps_half);
    }

    if (apps_state == kSmdSsOpening) {
        mem.WriteWord(apps_half + kHcTailOff, 0u);
    }

    if (mem.ReadWord(modem_half) != kSmdSsOpened) {
        OpenModemSmdHalf(modem_half);
    }
}

void Msm8255ModemPeer::ConsumeAppsSmdFlags(uint32_t cid, uint32_t rec,
                                           uint32_t apps_half_pa) {
    auto& mem = emu_.Get<EmulatedMemory>();
    if (mem.ReadByte(apps_half_pa + kHcFHeadOff) != 0u) {
        mem.WriteByte(apps_half_pa + kHcFHeadOff, 0u);
    }
    if (mem.ReadByte(apps_half_pa + kHcFTailOff) != 0u) {
        mem.WriteByte(apps_half_pa + kHcFTailOff, 0u);
    }
    if (mem.ReadByte(apps_half_pa + kHcFStateOff) != 0u) {
        mem.WriteByte(apps_half_pa + kHcFStateOff, 0u);
    }
    if (mem.ReadWord(apps_half_pa + kHcHeadOff) !=
        mem.ReadWord(apps_half_pa + kHcTailOff)) {
        ServiceSmdData(cid, rec, apps_half_pa);
    }
}

bool Msm8255ModemPeer::ChannelNameIs(uint32_t rec, const char* name,
                                     uint32_t bytes) {
    auto& mem = emu_.Get<EmulatedMemory>();
    for (uint32_t i = 0; i < bytes; ++i) {
        if (mem.ReadByte(rec + i) != (uint8_t)name[i]) return false;
    }
    return true;
}

bool Msm8255ModemPeer::ChannelIsQmuxControl(uint32_t rec) {
    for (const char* port : kQmuxControlPorts) {
        if (ChannelNameIs(rec, port,
                          static_cast<uint32_t>(std::strlen(port)) + 1u)) {
            return true;
        }
    }
    return false;
}

void Msm8255ModemPeer::HaltUnroutedSmdChannel(uint32_t cid, uint32_t rec) {
    auto& mem = emu_.Get<EmulatedMemory>();

    char name[kAllocElmNameBytes + 1] = {};
    for (uint32_t i = 0; i < kAllocElmNameBytes; ++i) {
        name[i] = static_cast<char>(mem.ReadByte(rec + i));
    }

    emu_.Get<Fatal>().Die(
        "msm8255 modem peer: smd channel %u \"%s\" carries data, and only the "
        "rpc router channel %u, the \"%s\" ports and the qmux control ports "
        "are modeled", cid, name, kRpcRouterCid, kDalPortName);
}

void Msm8255ModemPeer::ServiceSmdData(uint32_t cid, uint32_t rec,
                                      uint32_t apps_half_pa) {
    const bool to_router = cid == kRpcRouterCid;
    const bool to_dal    = !to_router &&
                        ChannelNameIs(rec, kDalPortName,
                                      sizeof(kDalPortName) - 1u);
    const bool to_qmux   = !to_router && !to_dal && ChannelIsQmuxControl(rec);
    if (!to_router && !to_dal && !to_qmux) HaltUnroutedSmdChannel(cid, rec);

    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t kind =
        mem.ReadWord(rec + kAllocElmCtypeOff) & kSmdKindMask;
    const bool kind_matches =
        to_qmux ? kind == kSmdKindPacket
                : kind == kSmdKindStream || kind == kSmdKindUnknown;
    if (!kind_matches) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: smd channel %u carries kind 0x%03X, and its "
            "route reads %s", cid, kind, to_qmux ? "packets" : "a stream");
    }
    uint32_t fifo_pa    = 0u;
    uint32_t fifo_bytes = 0u;
    if (!emu_.Get<Msm8255Smem>().ItemPaAndSize(kIdSmdFifoBase + cid, fifo_pa,
                                               fifo_bytes)) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: smd channel %u carries data and its fifo smem "
            "item %u is not allocated", cid, kIdSmdFifoBase + cid);
    }
    const uint32_t half = fifo_bytes / 2u;
    if ((fifo_bytes & 1u) != 0u || (half & kFifoWindowAlign) != 0u ||
        half < kFifoWindowMin || half > kFifoWindowMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: smd fifo smem item %u is %u bytes, so each "
            "direction gets %u, which the channel binding rejects",
            kIdSmdFifoBase + cid, fifo_bytes, half);
    }
    const uint32_t head = mem.ReadWord(apps_half_pa + kHcHeadOff);
    const uint32_t tail = mem.ReadWord(apps_half_pa + kHcTailOff);
    if (head >= half || tail >= half) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: smd fifo indices (head=%u tail=%u) leave the "
            "%u-byte half the channel binding gives each direction",
            head, tail, half);
    }

    const uint32_t modem_half = apps_half_pa + kHalfChannelBytes;
    uint32_t out_head = mem.ReadWord(modem_half + kHcHeadOff);
    const uint32_t out_tail = mem.ReadWord(modem_half + kHcTailOff);
    if (out_head >= half || out_tail >= half) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: the modem fifo indices (head=%u tail=%u) leave "
            "the %u-byte half the channel binding gives each direction",
            out_head, out_tail, half);
    }
    uint32_t cursor   = tail;
    uint32_t avail    = (head + half - tail) % half;
    uint32_t produced = 0u;
    while (avail != 0u) {
        const uint32_t run = half - cursor;
        uint32_t in_pa     = fifo_pa + cursor;
        uint32_t in_avail  = avail;
        if (avail > run) {
            auto& stage = emu_.Get<Msm8255SmdStage>();
            in_avail = stage.Linearize(fifo_pa, half, cursor, avail);
            in_pa    = stage.BasePa();
        }
        auto& out_stage = emu_.Get<Msm8255SmdStage>();
        const uint32_t out_pos = out_head % half;
        const uint32_t out_cap = SmdWriteAvail(half, out_pos, out_tail);
        uint32_t consumed = 0u;
        uint32_t sent     = 0u;
        if (to_router) {
            sent = emu_.Get<Msm8255RpcRouterPeer>().Answer(
                in_pa, in_avail, out_stage.WriteBasePa(), out_cap, consumed);
        } else if (to_dal) {
            sent = emu_.Get<Msm8255DalRemoteServer>().Answer(
                in_pa, in_avail, out_stage.WriteBasePa(), out_cap, consumed);
        } else {
            sent = AnswerQmuxControlPacket(in_pa, in_avail, half,
                                           out_stage.WriteBasePa(), out_cap,
                                           consumed);
        }
        if (consumed == 0u) break;
        if (sent != 0u) {
            out_stage.Scatter(fifo_pa + half, half, out_pos, sent);
        }
        cursor    = (cursor + consumed) % half;
        avail    -= consumed;
        out_head += sent;
        produced += sent;
    }

    if (cursor != tail) {
        mem.WriteWord(apps_half_pa + kHcTailOff, cursor);
        mem.WriteByte(modem_half + kHcFTailOff, 1u);
    }
    if (produced != 0u) {
        mem.WriteWord(modem_half + kHcHeadOff, out_head % half);
        mem.WriteByte(modem_half + kHcFHeadOff, 1u);
    }
    if (cursor != tail || produced != 0u) irq_->PulseIrq(kVicM2aSmd);
}

uint32_t Msm8255ModemPeer::AnswerQmuxControlPacket(uint32_t in_pa,
                                                   uint32_t in_avail,
                                                   uint32_t ring_bytes,
                                                   uint32_t out_pa,
                                                   uint32_t out_cap,
                                                   uint32_t& consumed) {
    consumed = 0u;
    if (in_avail < kSmdPacketHeaderBytes) return 0u;

    auto& mem = emu_.Get<EmulatedMemory>();
    uint8_t hdr[kSmdPacketHeaderBytes] = {};
    mem.CopyOut(in_pa, hdr, kSmdPacketHeaderBytes);
    const uint32_t len = cerf::le::U32(hdr, 0u);
    for (uint32_t off = 4u; off < kSmdPacketHeaderBytes; off += 4u) {
        if (cerf::le::U32(hdr, off) != 0u) {
            emu_.Get<Fatal>().Die(
                "msm8255 modem peer: smd packet header word +%u carries "
                "0x%08X, and only zero is modeled", off,
                cerf::le::U32(hdr, off));
        }
    }
    const uint32_t queued_max = ring_bytes - kAppsWriterSlackBytes;
    if (len > queued_max - kSmdPacketHeaderBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: a %u-byte smd packet and its %u-byte header "
            "exceed the %u bytes the apps writer keeps queued in a %u-byte fifo",
            len, kSmdPacketHeaderBytes, queued_max, ring_bytes);
    }
    if (len > Msm8255SmdStage::ReadCapacity() - kSmdPacketHeaderBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: a %u-byte smd packet exceeds the %u-byte read "
            "stage", len, Msm8255SmdStage::ReadCapacity());
    }
    if (in_avail < kSmdPacketHeaderBytes + len) return 0u;

    if (out_cap < kSmdPacketHeaderBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 modem peer: an smd packet reply needs its %u-byte header "
            "and the window at 0x%08X has %u", kSmdPacketHeaderBytes, out_pa,
            out_cap);
    }
    const uint32_t reply = emu_.Get<Msm8255QmuxPeer>().Answer(
        in_pa + kSmdPacketHeaderBytes, len, out_pa + kSmdPacketHeaderBytes,
        out_cap - kSmdPacketHeaderBytes);

    uint8_t out_hdr[kSmdPacketHeaderBytes] = {};
    cerf::le::Put32(out_hdr, reply);
    mem.CopyIn(out_pa, out_hdr, kSmdPacketHeaderBytes);

    consumed = kSmdPacketHeaderBytes + len;
    return kSmdPacketHeaderBytes + reply;
}

void Msm8255ModemPeer::OpenModemSmdHalf(uint32_t modem_half_pa) {
    auto& mem = emu_.Get<EmulatedMemory>();
    mem.WriteByte(modem_half_pa + kHcFDsrOff, 1u);
    mem.WriteByte(modem_half_pa + kHcFCtsOff, 1u);
    mem.WriteByte(modem_half_pa + kHcFCdOff, 1u);
    mem.WriteWord(modem_half_pa, kSmdSsOpened);
    mem.WriteByte(modem_half_pa + kHcFStateOff, 1u);
    irq_->PulseIrq(kVicM2aSmd);
}

void Msm8255ModemPeer::RunProcComm() {
    auto& smem = emu_.Get<Msm8255Smem>();
    auto& mem  = emu_.Get<EmulatedMemory>();
    const uint32_t cmd = mem.ReadWord(smem.SmemPa() + kAppCommandOff);
    emu_.Get<Fatal>().Die(
        "msm8255 modem peer: proc_comm command %u is not modeled", cmd);
}

REGISTER_SERVICE(Msm8255ModemPeer);

}
