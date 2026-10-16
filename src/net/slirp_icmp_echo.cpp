#include "slirp_backend.h"

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../platform/host_icmp_echo.h"
#include <cerfcore/state/emulation_freeze.h>
#include "ipv4_packet.h"

namespace cerfcore {

namespace {

using cerf::be::Put16;
using cerf::be::U16;
using namespace cerf::inet;

/* Identify an outbound IPv4 ICMP echo request. Returns false if the
   frame is anything else (caller falls through to slirp_input). */
struct IcmpEchoRequest {
    MacAddress guest_mac;
    uint32_t guest_ip_n;   /* network-byte-order */
    uint32_t dest_ip_n;    /* network-byte-order */
    uint16_t id;           /* host order */
    uint16_t seq;          /* host order */
    std::vector<uint8_t> payload;
};

bool ParseIcmpEchoRequest(const uint8_t* frame, size_t len,
                          IcmpEchoRequest& out) {
    if (len < kEthHeaderSize + kIpv4HeaderSize + kIcmpHeaderSize) return false;
    if (EthType(frame) != kEthTypeIpv4) return false;
    const uint8_t* ip = frame + kEthHeaderSize;
    const uint32_t ihl = Ipv4HeaderLen(ip);
    if (ihl < kIpv4HeaderSize || kEthHeaderSize + ihl + kIcmpHeaderSize > len) return false;
    if (ip[kIpOffProto] != kIpProtoIcmp) return false;
    const uint8_t* icmp = frame + kEthHeaderSize + ihl;
    if (icmp[kIcmpOffType] != kIcmpTypeEchoRequest || icmp[kIcmpOffCode] != 0) return false;
    std::memcpy(out.guest_mac.data(), frame + kEthOffSrc, kEthMacSize);
    std::memcpy(&out.guest_ip_n, ip + kIpOffSrc, kIpv4AddrSize);
    std::memcpy(&out.dest_ip_n, ip + kIpOffDst, kIpv4AddrSize);
    out.id  = U16(icmp, kIcmpOffId);
    out.seq = U16(icmp, kIcmpOffSeq);
    out.payload.assign(icmp + kIcmpHeaderSize, frame + len);
    return true;
}

std::vector<uint8_t> BuildIcmpEchoReplyFrame(const IcmpEchoRequest& req,
                                             const std::vector<uint8_t>& reply_data,
                                             uint16_t mtu_cap) {
    std::vector<uint8_t> out;
    size_t icmp_data_len = reply_data.size();
    size_t max_data = (size_t)mtu_cap - kEthHeaderSize - kIpv4HeaderSize - kIcmpHeaderSize;
    if (icmp_data_len > max_data) return out;  /* empty = caller drops */
    out.assign(kEthHeaderSize + kIpv4HeaderSize + kIcmpHeaderSize + icmp_data_len, 0);

    PutEthHeader(out.data(), req.guest_mac.data(), NetworkBackend::kHostGatewayMac.data(),
                 kEthTypeIpv4);

    uint8_t* oip = out.data() + kEthHeaderSize;
    WriteIpv4Header(oip, static_cast<uint16_t>(kIpv4HeaderSize + kIcmpHeaderSize + icmp_data_len),
                    kIpProtoIcmp,
                    reinterpret_cast<const uint8_t*>(&req.dest_ip_n),
                    reinterpret_cast<const uint8_t*>(&req.guest_ip_n));

    uint8_t* oicmp = out.data() + kEthHeaderSize + kIpv4HeaderSize;
    oicmp[kIcmpOffType] = kIcmpTypeEchoReply; oicmp[kIcmpOffCode] = 0;
    Put16(oicmp + kIcmpOffChecksum, 0);
    Put16(oicmp + kIcmpOffId, req.id);
    Put16(oicmp + kIcmpOffSeq, req.seq);
    if (icmp_data_len)
        std::memcpy(oicmp + kIcmpHeaderSize, reply_data.data(), icmp_data_len);
    Put16(oicmp + kIcmpOffChecksum, InetChecksum(oicmp, kIcmpHeaderSize + icmp_data_len));
    return out;
}

} /* namespace */

bool SlirpBackend::TryInterceptIcmpEcho(const uint8_t* frame, std::size_t len) {
    IcmpEchoRequest req;
    if (!ParseIcmpEchoRequest(frame, len, req)) return false;

    uint16_t mtu_cap = (uint16_t)mtu_;

    auto done = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<void> mark_done(nullptr, [done](void*) {
        done->store(true, std::memory_order_release);
    });

    auto echo = [this, req = std::move(req), mtu_cap, mark_done]() mutable {
        const host_platform::IcmpEchoResult result = host_platform::SendIcmpEcho(
            req.dest_ip_n, req.payload.data(), req.payload.size(), 1000);
        if (result.status == host_platform::IcmpEchoStatus::kOpenFailed) return;
        if (result.status == host_platform::IcmpEchoStatus::kNoReply) {
            LOG(Net, "ICMP echo to 0x%08X: no reply (err=%u)\n",
                req.dest_ip_n, result.code);
            return;
        }
        if (result.status == host_platform::IcmpEchoStatus::kFailed) {
            LOG(Net, "ICMP echo to 0x%08X: status=%u\n",
                req.dest_ip_n, result.code);
            return;
        }
        auto reply_frame = BuildIcmpEchoReplyFrame(req, result.data, mtu_cap);
        if (reply_frame.empty()) return;

        auto frozen = emu_.Get<EmulationFreeze>().WorkerSection();
        DispatchFrame(reply_frame.data(), reply_frame.size());
    };

    std::lock_guard<std::mutex> lk(icmp_mutex_);
    if (icmp_stopping_) return true;   /* shutdown joined the live set; drop the echo */

    for (auto it = icmp_threads_.begin(); it != icmp_threads_.end();) {
        if (it->done->load(std::memory_order_acquire)) {
            it->thread.join();
            it = icmp_threads_.erase(it);
        } else {
            ++it;
        }
    }
    icmp_threads_.push_back({std::thread(std::move(echo)), std::move(done)});
    return true;
}

void SlirpBackend::JoinIcmpThreads() {
    std::vector<IcmpEcho> live;
    {
        std::lock_guard<std::mutex> lk(icmp_mutex_);
        icmp_stopping_ = true;
        live.swap(icmp_threads_);
    }
    for (auto& e : live)
        if (e.thread.joinable()) e.thread.join();
}

}
