#include "host_icmp_echo.h"

#include "../net/ipv4_packet.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#if !defined(__linux__)
#error "host_icmp_echo_posix.cpp has no ICMP echo socket for this host"
#endif

namespace cerfcore {

namespace host_platform {

namespace {

constexpr size_t kReplyBytes = 65536u;

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
    SocketFd       sock(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_ICMP));
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
        const ssize_t n = ::recv(sock.Get(), reply.data(), reply.size(), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            out.status = IcmpEchoStatus::kFailed;
            out.code   = static_cast<uint32_t>(errno);
            return out;
        }
        if (static_cast<size_t>(n) < inet::kIcmpHeaderSize ||
            reply[inet::kIcmpOffType] != inet::kIcmpTypeEchoReply)
            continue;
        out.status = IcmpEchoStatus::kReply;
        out.data.assign(reply.data() + inet::kIcmpHeaderSize, reply.data() + n);
        return out;
    }
    out.status = IcmpEchoStatus::kNoReply;
    out.code   = static_cast<uint32_t>(ETIMEDOUT);
    return out;
}

}

}
