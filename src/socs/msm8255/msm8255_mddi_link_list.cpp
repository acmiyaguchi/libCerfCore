#include "msm8255_mddi_link_list.h"

#include "msm8255_mddi_client.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace {

/* Linux arch/arm/mach-msm video-msm mddihosti.h:
   mddi_host_llist_struct. */
constexpr uint32_t kItemFlagsOff       =  0u;
constexpr uint32_t kItemHeaderCountOff =  2u;
constexpr uint32_t kItemDataCountOff   =  4u;
constexpr uint32_t kItemDataPtrOff     =  6u;
constexpr uint32_t kItemNextPtrOff     = 10u;
constexpr uint32_t kItemPacketOff      = 16u;

/* Linux arch/arm/mach-msm video-msm mddihosti.h:
   mddi_register_access_packet_type. */
constexpr uint32_t kPktLengthOff   =  0u;
constexpr uint32_t kPktTypeOff     =  2u;
constexpr uint32_t kPktClientIdOff =  4u;
constexpr uint32_t kPktRwInfoOff   =  6u;
constexpr uint32_t kPktAddressOff  =  8u;

/* Linux arch/arm/mach-msm video-msm mddihosti.h:
   mddi_video_stream_packet_type, whose fields follow bClient_ID. */
constexpr uint32_t kVidFormatOff     =  6u;
constexpr uint32_t kVidAttributesOff =  8u;
constexpr uint32_t kVidLeftEdgeOff   = 10u;
constexpr uint32_t kVidTopEdgeOff    = 12u;
constexpr uint32_t kVidRightEdgeOff  = 14u;
constexpr uint32_t kVidBottomEdgeOff = 16u;
constexpr uint32_t kVidXStartOff     = 18u;
constexpr uint32_t kVidYStartOff     = 20u;
constexpr uint32_t kVidPixelCountOff = 22u;

constexpr uint32_t kTypeRegisterAccess = 146u;
constexpr uint32_t kTypeVideoStream    =  16u;

constexpr uint32_t kRegisterAccessHeaderBytes = 14u;
constexpr uint32_t kVideoStreamHeaderBytes    = 26u;

constexpr uint32_t kClientIdBroadcast = 0u;

/* Linux arch/arm/mach-msm video-msm mddihosti.h: read_write_info carries the
   count of 32-bit register data items in bits 13:0, and the direction in bits
   15:14, where 00 writes registers and 10 reads them. */
constexpr uint32_t kRwCountMask = 0x3FFFu;
constexpr uint32_t kRwOpShift   = 14u;
constexpr uint32_t kRwOpWrite   = 0u;
constexpr uint32_t kRwOpRead    = 2u;

constexpr uint32_t kOneDataItem   = 1u;
constexpr uint32_t kDataItemBytes = 4u;

/* Linux arch/arm/mach-msm video-msm mddihosti.c: appending to the chain clears
   the end flag of the item that was last, so bit 0 marks the final item. */
constexpr uint32_t kFlagEndOfList = 0x0001u;

constexpr uint32_t kFlagsAccepted = kFlagEndOfList | 0x0010u;

constexpr uint32_t kMaxChainItems = 25u;

}

bool Msm8255MddiLinkList::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

uint32_t Msm8255MddiLinkList::ReadUnalignedWord(uint32_t pa) {
    auto& mem = emu_.Get<EmulatedMemory>();
    return mem.ReadHalf(pa) |
           (static_cast<uint32_t>(mem.ReadHalf(pa + 2u)) << 16);
}

void Msm8255MddiLinkList::Execute(uint32_t head_pa,
                                  Msm8255MddiLinkListHost& host) {
    uint32_t item  = head_pa;
    uint32_t walked = 0u;
    while (item != 0u) {
        if (walked == kMaxChainItems) {
            emu_.Get<Fatal>().Die(
                "msm8255 mddi link list: the chain at 0x%08X still runs after "
                "%u items", head_pa, walked);
        }
        ExecuteItem(item, host);
        item = ReadUnalignedWord(item + kItemNextPtrOff);
        ++walked;
    }
}

void Msm8255MddiLinkList::ExecuteItem(uint32_t item_pa,
                                      Msm8255MddiLinkListHost& host) {
    auto& mem = emu_.Get<EmulatedMemory>();

    const uint32_t flags = mem.ReadHalf(item_pa + kItemFlagsOff);
    if ((flags & ~kFlagsAccepted) != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 mddi link list: the item at 0x%08X carries link "
            "controller flags 0x%04X", item_pa, flags);
    }

    const uint32_t packet_pa    = item_pa + kItemPacketOff;
    const uint32_t type         = mem.ReadHalf(packet_pa + kPktTypeOff);
    const uint32_t header_bytes = mem.ReadHalf(item_pa + kItemHeaderCountOff);
    const uint32_t data_bytes   = mem.ReadHalf(item_pa + kItemDataCountOff);

    if (type == kTypeRegisterAccess) {
        RequireHeaderBytes(item_pa, header_bytes, kRegisterAccessHeaderBytes);
    } else if (type == kTypeVideoStream) {
        RequireHeaderBytes(item_pa, header_bytes, kVideoStreamHeaderBytes);
    } else {
        emu_.Get<Fatal>().Die(
            "msm8255 mddi link list: the item at 0x%08X sends packet type %u",
            item_pa, type);
    }

    RequireFraming(item_pa, packet_pa, header_bytes, data_bytes);

    if (type == kTypeRegisterAccess) {
        ExecuteRegisterAccess(item_pa, packet_pa, data_bytes, host);
        return;
    }
    ExecuteVideoStream(item_pa, packet_pa, data_bytes);
}

void Msm8255MddiLinkList::RequireHeaderBytes(uint32_t item_pa, uint32_t have,
                                             uint32_t want) {
    if (have == want) return;
    emu_.Get<Fatal>().Die(
        "msm8255 mddi link list: the item at 0x%08X sends a %u-byte packet "
        "header where its packet type carries %u", item_pa, have, want);
}

void Msm8255MddiLinkList::RequireFraming(uint32_t item_pa, uint32_t packet_pa,
                                         uint32_t header_bytes,
                                         uint32_t data_bytes) {
    auto& mem = emu_.Get<EmulatedMemory>();

    const uint32_t client_id = mem.ReadHalf(packet_pa + kPktClientIdOff);
    if (client_id != kClientIdBroadcast) {
        emu_.Get<Fatal>().Die(
            "msm8255 mddi link list: the packet at 0x%08X addresses client "
            "id %u", item_pa, client_id);
    }

    const uint32_t length = mem.ReadHalf(packet_pa + kPktLengthOff);
    if (length != header_bytes + data_bytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 mddi link list: the packet at 0x%08X declares %u bytes "
            "over a %u-byte header and %u bytes of data", item_pa, length,
            header_bytes, data_bytes);
    }
}

void Msm8255MddiLinkList::ExecuteRegisterAccess(
    uint32_t item_pa, uint32_t packet_pa, uint32_t data_bytes,
    Msm8255MddiLinkListHost& host) {
    auto& mem = emu_.Get<EmulatedMemory>();

    const uint32_t rw_info = mem.ReadHalf(packet_pa + kPktRwInfoOff);
    const uint32_t count   = rw_info & kRwCountMask;
    if (count != kOneDataItem) {
        emu_.Get<Fatal>().Die(
            "msm8255 mddi link list: the register access at 0x%08X transfers "
            "%u data items", item_pa, count);
    }

    const uint32_t address = ReadUnalignedWord(packet_pa + kPktAddressOff);
    auto&          client  = emu_.Get<Msm8255MddiClient>();

    const uint32_t op = rw_info >> kRwOpShift;
    if (op == kRwOpRead) {
        host.QueueRegisterReadResponse(address, client.ReadRegister(address));
        return;
    }
    if (op != kRwOpWrite) {
        emu_.Get<Fatal>().Die(
            "msm8255 mddi link list: the register access at 0x%08X carries "
            "read_write_info 0x%04X", item_pa, rw_info);
    }

    if (data_bytes != kDataItemBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 mddi link list: the register write at 0x%08X sends %u "
            "data bytes", item_pa, data_bytes);
    }

    const uint32_t data_pa = ReadUnalignedWord(item_pa + kItemDataPtrOff);
    client.WriteRegister(address, ReadUnalignedWord(data_pa));
}

void Msm8255MddiLinkList::ExecuteVideoStream(uint32_t item_pa,
                                             uint32_t packet_pa,
                                             uint32_t data_bytes) {
    auto& mem = emu_.Get<EmulatedMemory>();

    Msm8255MddiVideoStream video{};
    video.format_descriptor =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidFormatOff));
    video.pixel_attributes =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidAttributesOff));
    video.x_left_edge =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidLeftEdgeOff));
    video.y_top_edge =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidTopEdgeOff));
    video.x_right_edge =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidRightEdgeOff));
    video.y_bottom_edge =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidBottomEdgeOff));
    video.x_start =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidXStartOff));
    video.y_start =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidYStartOff));
    video.pixel_count =
        static_cast<uint16_t>(mem.ReadHalf(packet_pa + kVidPixelCountOff));

    const uint32_t data_pa = ReadUnalignedWord(item_pa + kItemDataPtrOff);
    emu_.Get<Msm8255MddiClient>().WriteVideoStream(video, data_pa, data_bytes);
}

REGISTER_SERVICE(Msm8255MddiLinkList);
