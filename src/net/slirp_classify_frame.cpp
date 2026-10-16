#include "slirp_backend_internal.h"

#include <cerfcore/core/byte_order.h>
#include "ipv4_packet.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cerfcore {

using cerf::be::U16;
using namespace cerf::inet;

void ClassifyFrame(const uint8_t* f, std::size_t len,
                   char* out, std::size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (len < kEthHeaderSize) { std::snprintf(out, out_len, "short<14"); return; }
    uint16_t etype = EthType(f);
    if (etype == kEthTypeArp) {
        const uint8_t* arp = f + kEthHeaderSize;
        if (len >= kEthHeaderSize + kArpPacketSize) {
            uint16_t op = U16(arp, kArpOffOper);
            const uint8_t* sip = arp + kArpOffSenderIp;
            const uint8_t* tip = arp + kArpOffTargetIp;
            std::snprintf(out, out_len,
                          "arp op=%u sender=%u.%u.%u.%u target=%u.%u.%u.%u", op,
                          sip[0], sip[1], sip[2], sip[3], tip[0], tip[1], tip[2], tip[3]);
        } else if (len >= kEthHeaderSize + kArpOffOper + 2) {
            uint16_t op = U16(arp, kArpOffOper);
            std::snprintf(out, out_len, "arp op=%u", op);
        } else {
            std::snprintf(out, out_len, "arp");
        }
        return;
    }
    if (etype == kEthTypeIpv4 && len >= kEthHeaderSize + kIpv4HeaderSize) {
        const uint8_t* ip = f + kEthHeaderSize;
        uint8_t proto = ip[kIpOffProto];
        uint8_t ihl = static_cast<uint8_t>(Ipv4HeaderLen(ip));
        if (len < kEthHeaderSize + ihl) {
            std::snprintf(out, out_len, "ipv4 proto=%u ihl=%u short", proto, ihl);
            return;
        }
        const uint8_t* l4 = ip + ihl;
        const uint8_t* sa = ip + kIpOffSrc;
        const uint8_t* da = ip + kIpOffDst;
        char sip[16] = {}, dip[16] = {};
        std::snprintf(sip, sizeof(sip), "%u.%u.%u.%u",
                      sa[0], sa[1], sa[2], sa[3]);
        std::snprintf(dip, sizeof(dip), "%u.%u.%u.%u",
                      da[0], da[1], da[2], da[3]);
        if (proto == kIpProtoUdp && len >= kEthHeaderSize + ihl + kUdpHeaderSize) {
            uint16_t sp = U16(l4, kUdpOffSrcPort);
            uint16_t dp = U16(l4, kUdpOffDstPort);
            const char* hint = (sp == kUdpPortDhcpClient || dp == kUdpPortDhcpClient ||
                                sp == kUdpPortDhcpServer || dp == kUdpPortDhcpServer)
                ? " dhcp"
                : (sp == kUdpPortDns || dp == kUdpPortDns ? " dns" : "");
            std::snprintf(out, out_len,
                          "ipv4 udp %s:%u -> %s:%u%s", sip, sp, dip, dp, hint);
        } else if (proto == kIpProtoTcp && len >= kEthHeaderSize + ihl + kTcpHeaderSize) {
            uint16_t sp = U16(l4, kTcpOffSrcPort);
            uint16_t dp = U16(l4, kTcpOffDstPort);
            uint8_t  flg = l4[kTcpOffFlags];
            char flags[8] = {};
            int n = 0;
            if (flg & 0x02) flags[n++] = 'S';
            if (flg & 0x10) flags[n++] = 'A';
            if (flg & 0x01) flags[n++] = 'F';
            if (flg & 0x04) flags[n++] = 'R';
            if (flg & 0x08) flags[n++] = 'P';
            if (n == 0) flags[n++] = '.';
            std::snprintf(out, out_len,
                          "ipv4 tcp %s:%u -> %s:%u [%s]", sip, sp, dip, dp, flags);
        } else if (proto == kIpProtoIcmp) {
            std::snprintf(out, out_len,
                          "ipv4 icmp %s -> %s", sip, dip);
        } else {
            std::snprintf(out, out_len,
                          "ipv4 proto=%u %s -> %s", proto, sip, dip);
        }
        return;
    }
    if (etype == kEthTypeIpv6) {
        std::snprintf(out, out_len, "ipv6");
        return;
    }
    std::snprintf(out, out_len, "etype=0x%04X", etype);
}

}
