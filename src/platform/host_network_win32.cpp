#include "host_network.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <vector>

namespace cerfcore {

namespace host_platform {

bool StartNetworking(int& error) {
    WSADATA wsa{};
    error = WSAStartup(MAKEWORD(2, 2), &wsa);
    return error == 0;
}

TcpConnectProbe ProbeTcpConnect6(const uint8_t (&address)[16], uint16_t port, int timeout_ms) {
    TcpConnectProbe out;
    SOCKET s = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        out.error = WSAGetLastError();
        return out;
    }
    out.socket_created = true;
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);
    sockaddr_in6 dst{};
    dst.sin6_family = AF_INET6;
    dst.sin6_port   = htons(port);
    std::memcpy(&dst.sin6_addr, address, sizeof(address));
    int rc  = ::connect(s, (sockaddr*)&dst, sizeof(dst));
    int err = (rc == 0) ? 0 : WSAGetLastError();
    if (rc == 0) {
        out.reachable = true;
    } else if (err == WSAEWOULDBLOCK) {
        WSAPOLLFD pfd{};
        pfd.fd = s;
        pfd.events = POLLWRNORM;
        int pr = WSAPoll(&pfd, 1, timeout_ms);
        if (pr > 0 && (pfd.revents & POLLWRNORM) &&
            !(pfd.revents & (POLLERR | POLLHUP))) {
            int so_err = 0;
            int len = (int)sizeof(so_err);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_err, &len) == 0
                && so_err == 0) {
                out.reachable = true;
            } else {
                err = so_err;
            }
        }
    }
    closesocket(s);
    out.error = out.reachable ? 0 : err;
    return out;
}

struct HostPollSet::Entries {
    std::vector<WSAPOLLFD> fds;
};

HostPollSet::HostPollSet() : entries_(std::make_unique<Entries>()) {}
HostPollSet::~HostPollSet() = default;

void HostPollSet::Clear() { entries_->fds.clear(); }
bool HostPollSet::Empty() const { return entries_->fds.empty(); }

int HostPollSet::Add(uintptr_t socket, int events) {
    WSAPOLLFD pfd{};
    pfd.fd = static_cast<SOCKET>(socket);
    if (events & kPollReadable) pfd.events |= POLLRDNORM;
    if (events & kPollWritable) pfd.events |= POLLWRNORM;
    if (events & kPollPriority) pfd.events |= POLLRDBAND;
    entries_->fds.push_back(pfd);
    return (int)entries_->fds.size() - 1;
}

int HostPollSet::Revents(int index) const {
    if (index < 0 || index >= (int)entries_->fds.size()) return 0;
    const short revents = entries_->fds[index].revents;
    int out = 0;
    if (revents & POLLRDNORM) out |= kPollReadable;
    if (revents & POLLWRNORM) out |= kPollWritable;
    if (revents & POLLRDBAND) out |= kPollPriority;
    if (revents & POLLERR)    out |= kPollError;
    if (revents & POLLHUP)    out |= kPollHangUp;
    return out;
}

int HostPollSet::Wait(int timeout_ms) {
    return WSAPoll(entries_->fds.data(), (ULONG)entries_->fds.size(), timeout_ms);
}

}

}
