#include "host_icmp_echo.h"

#include <cerfcore/platform/host_descriptor.h>

#include "../net/ipv4_packet.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

namespace {

constexpr size_t kReplyBytes = 65536u;

/* libslirp icmp_send / icmp_receive: a Darwin SOCK_DGRAM IPPROTO_ICMP socket returns
   the IP header in front of the ICMP message, a Linux one does not. */
#if defined(__linux__)
constexpr bool kReplyHasIpHeader = false;
#elif defined(__APPLE__)
constexpr bool kReplyHasIpHeader = true;
#else
#error "host_icmp_echo_posix.cpp does not know the ICMP datagram socket format of this host"
#endif

constexpr size_t kIpv4MinHeader = 20u;

int OpenIcmpSocket() {
#if defined(SOCK_CLOEXEC)
    return ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_ICMP);
#else
    const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd >= 0 && !SetCloseOnExec(fd)) {
        ::close(fd);
        return -1;
    }
    return fd;
#endif
}

size_t IcmpOffset(const uint8_t* packet, size_t size) {
    if (!kReplyHasIpHeader) return 0u;
    if (size < kIpv4MinHeader) return size;
    const size_t header = static_cast<size_t>(packet[0] & 0x0Fu) * 4u;
    return header < kIpv4MinHeader || header > size ? size : header;
}

class SocketFd {
public:
    explicit SocketFd(int fd) : fd_(fd) {}
    ~SocketFd() {
        if (fd_ >= 0) ::close(fd_);
    }
    SocketFd(const SocketFd&)            = delete;
    SocketFd& operator=(const SocketFd&) = delete;
    int Get() const { return fd_; }

private:
    int fd_;
};

}

IcmpEchoResult SendIcmpEcho(uint32_t dest_ip_n, const uint8_t* payload, size_t size,
                            uint32_t timeout_ms) {
    namespace inet = cerf::inet;
    IcmpEchoResult out;
    SocketFd       sock(OpenIcmpSocket());
    if (sock.Get() < 0) return out;

    std::vector<uint8_t> request(inet::kIcmpHeaderSize + size, 0u);
    request[inet::kIcmpOffType] = inet::kIcmpTypeEchoRequest;
    if (size != 0u) std::memcpy(request.data() + inet::kIcmpHeaderSize, payload, size);
    cerf::be::Put16(request.data() + inet::kIcmpOffChecksum,
              inet::InetChecksum(request.data(), request.size()));

    sockaddr_in to{};
    to.sin_family      = AF_INET;
    to.sin_addr.s_addr = dest_ip_n;
    if (::sendto(sock.Get(), request.data(), request.size(), 0,
                 reinterpret_cast<const sockaddr*>(&to), sizeof(to)) < 0) {
        out.status = IcmpEchoStatus::kNoReply;
        out.code   = static_cast<uint32_t>(errno);
        return out;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::vector<uint8_t> reply(kReplyBytes);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) break;
        pollfd pfd{sock.Get(), POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(left.count()));
        if (ready < 0) {
            if (errno == EINTR) continue;
            out.status = IcmpEchoStatus::kFailed;
            out.code   = static_cast<uint32_t>(errno);
            return out;
        }
        if (ready == 0) break;
        sockaddr_in   from{};
        socklen_t     from_size = sizeof(from);
        const ssize_t n = ::recvfrom(sock.Get(), reply.data(), reply.size(), 0,
                                     reinterpret_cast<sockaddr*>(&from), &from_size);
        if (n < 0) {
            if (errno == EINTR) continue;
            out.status = IcmpEchoStatus::kFailed;
            out.code   = static_cast<uint32_t>(errno);
            return out;
        }
        const size_t   offset = IcmpOffset(reply.data(), static_cast<size_t>(n));
        const uint8_t* icmp   = reply.data() + offset;
        const size_t   length = static_cast<size_t>(n) - offset;
        if (length < inet::kIcmpHeaderSize || icmp[inet::kIcmpOffType] != inet::kIcmpTypeEchoReply ||
            from.sin_addr.s_addr != dest_ip_n || length - inet::kIcmpHeaderSize != size ||
            (size != 0u && std::memcmp(icmp + inet::kIcmpHeaderSize, payload, size) != 0)) {
            continue;
        }
        out.status = IcmpEchoStatus::kReply;
        out.data.assign(icmp + inet::kIcmpHeaderSize, icmp + length);
        return out;
    }
    out.status = IcmpEchoStatus::kNoReply;
    out.code   = static_cast<uint32_t>(ETIMEDOUT);
    return out;
}

}

}
